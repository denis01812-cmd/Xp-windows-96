# Boot the real packaged kernel in native Windows QEMU, not a test executable.
param([Parameter(Mandatory = $true)][string]$PackagePath)
$ErrorActionPreference = 'Stop'

$qemu = @('qemu-system-i386.exe', 'qemu-system-x86_64.exe') |
    ForEach-Object { Join-Path $env:ProgramFiles ('qemu\' + $_) } |
    Where-Object { [System.IO.File]::Exists($_) } | Select-Object -First 1
if (-not $qemu) { throw 'The CI Windows QEMU installation was not found.' }

$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ('xp96-qemu-test-' + [guid]::NewGuid())
$logs = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\windows-qemu-logs'
$null = New-Item -ItemType Directory -Path $logs -Force
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$utf8 = [System.Text.UTF8Encoding]::new($false)

try {
    $unicode = -join ([char[]](0x0432, 0x0444, 0x0432, 0x0444))
    $unicodeRoot = Join-Path $temporary ('Package, [' + $unicode + '] & spaces !')
    $asciiRoot = Join-Path $temporary 'ASCII-control'
    foreach ($root in @($asciiRoot, $unicodeRoot)) {
        $null = New-Item -ItemType Directory -Path $root
        $null = New-Item -ItemType Directory -Path (Join-Path $root 'tools')
        foreach ($name in @('Start-XP96.bat', 'xp96.img', 'tools\run-windows.ps1')) {
            Copy-Item -LiteralPath (Join-Path $PackagePath $name) -Destination (Join-Path $root $name)
        }
    }
    $originalHash = (Get-FileHash -LiteralPath (Join-Path $asciiRoot 'xp96.img') -Algorithm SHA256).Hash
    $renamedImage = Join-Path $unicodeRoot ($unicode + '.img')
    Copy-Item -LiteralPath (Join-Path $unicodeRoot 'xp96.img') -Destination $renamedImage
    $unicodeTemp = Join-Path $temporary ($unicode + ' TEMP')
    $null = New-Item -ItemType Directory -Path $unicodeTemp

    foreach ($case in @('ascii-control', 'unicode-directory', 'unicode-filename-and-temp')) {
        $root = if ($case -eq 'ascii-control') { $asciiRoot } else { $unicodeRoot }
        $scriptPath = Join-Path $root 'tools\run-windows.ps1'
        $parameters = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $scriptPath, '-QemuPath', $qemu, '-Headless')
        if ($case -eq 'unicode-filename-and-temp') {
            $parameters += @('-ImagePath', $renamedImage)
            $env:TEMP = $unicodeTemp
            $env:TMP = $unicodeTemp
        }
        $start = [System.Diagnostics.ProcessStartInfo]::new()
        $start.FileName = "$PSHOME\powershell.exe"
        $start.Arguments = ($parameters | ForEach-Object { '"' + $_ + '"' }) -join ' '
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        # Keep a valid stdin pipe open: Windows QEMU's stdio chardev needs an
        # actual input handle even when this test never sends serial input.
        $start.RedirectStandardInput = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $start.StandardOutputEncoding = $utf8
        $start.StandardErrorEncoding = $utf8
        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $start
        $output = [System.Text.StringBuilder]::new()
        $ready = $false
        try {
            $null = $process.Start()
            $errorTask = $process.StandardError.ReadToEndAsync()
            $lineTask = $process.StandardOutput.ReadLineAsync()
            $deadline = [DateTime]::UtcNow.AddSeconds(45)
            do {
                if ($lineTask.IsCompleted) {
                    $line = $lineTask.Result
                    if ($null -eq $line) { break }
                    $null = $output.AppendLine($line)
                    if ($line.Contains('XP96:PANIC')) { throw ("Kernel panic: " + $output) }
                    if ($line.Contains('XP96:DESKTOP_READY')) { $ready = $true; break }
                    $lineTask = $process.StandardOutput.ReadLineAsync()
                } else {
                    if ($process.HasExited -and -not $lineTask.Wait(1000)) { break }
                    Start-Sleep -Milliseconds 20
                }
            } while ([DateTime]::UtcNow -lt $deadline)
        } finally {
            # Stop only this launcher's VM. Let its finally block clean up a
            # temporary image before terminating the parent as a last resort.
            if (-not $process.HasExited) {
                Get-CimInstance Win32_Process -Filter ("ParentProcessId = " + $process.Id) |
                    Where-Object { $_.Name -like 'qemu-system-*.exe' } |
                    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
                if (-not $process.WaitForExit(10000)) {
                    $null = & taskkill.exe /PID $process.Id /T /F 2>&1
                    $process.WaitForExit()
                }
            }
            $errorText = $errorTask.Result
            [System.IO.File]::WriteAllText((Join-Path $logs ($case + '.stdout.log')), $output.ToString(), $utf8)
            [System.IO.File]::WriteAllText((Join-Path $logs ($case + '.stderr.log')), $errorText, $utf8)
            $process.Dispose()
        }
        if (-not $ready) { throw ("Native QEMU boot failed ($case):`n$output`n$errorText") }
        Write-Host ("PASS real Windows QEMU boot to DESKTOP_READY: " + $case)
    }
    foreach ($image in @((Join-Path $asciiRoot 'xp96.img'), (Join-Path $unicodeRoot 'xp96.img'), $renamedImage)) {
        if ((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -ne $originalHash) {
            throw 'An original disk image was modified.'
        }
    }
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    if ([System.IO.Directory]::Exists($temporary)) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
