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

try {
    $unicode = -join ([char[]](0x0432, 0x0444, 0x0432, 0x0444))
    $root = Join-Path $temporary ('Package, [' + $unicode + '] & spaces !')
    $null = New-Item -ItemType Directory -Path $root
    $null = New-Item -ItemType Directory -Path (Join-Path $root 'tools')
    foreach ($name in @('Start-XP96.bat', 'xp96.img', 'tools\run-windows.ps1')) {
        Copy-Item -LiteralPath (Join-Path $PackagePath $name) -Destination (Join-Path $root $name)
    }
    $scriptPath = Join-Path $root 'tools\run-windows.ps1'
    $image = Join-Path $root 'xp96.img'
    $originalHash = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
    $renamedImage = Join-Path $root ($unicode + '.img')
    Copy-Item -LiteralPath $image -Destination $renamedImage
    $unicodeTemp = Join-Path $temporary ($unicode + ' TEMP')
    $null = New-Item -ItemType Directory -Path $unicodeTemp

    foreach ($case in @('unicode-directory', 'unicode-filename-and-temp')) {
        $stdout = Join-Path $logs ($case + '.stdout.log')
        $stderr = Join-Path $logs ($case + '.stderr.log')
        $parameters = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $scriptPath, '-QemuPath', $qemu, '-Headless')
        if ($case -eq 'unicode-filename-and-temp') {
            $parameters += @('-ImagePath', $renamedImage)
            $env:TEMP = $unicodeTemp
            $env:TMP = $unicodeTemp
        }
        $argumentString = ($parameters | ForEach-Object { '"' + $_ + '"' }) -join ' '
        $process = Start-Process -FilePath "$PSHOME\powershell.exe" -ArgumentList $argumentString -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $ready = $false
        try {
            $deadline = [DateTime]::UtcNow.AddSeconds(45)
            do {
                if ([System.IO.File]::Exists($stdout)) {
                    $output = [System.IO.File]::ReadAllText($stdout)
                    if ($output.Contains('XP96:PANIC')) { throw ("Kernel panic: " + $output) }
                    if ($output.Contains('XP96:DESKTOP_READY')) { $ready = $true; break }
                }
                if ($process.HasExited) { break }
                Start-Sleep -Milliseconds 100
            } while ([DateTime]::UtcNow -lt $deadline)
        } finally {
            # Stop only the VM belonging to this launcher, allowing its finally
            # block to remove any temporary copy before terminating the parent.
            if (-not $process.HasExited) {
                Get-CimInstance Win32_Process -Filter ("ParentProcessId = " + $process.Id) |
                    Where-Object { $_.Name -like 'qemu-system-*.exe' } |
                    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
                if (-not $process.WaitForExit(10000)) {
                    $null = & taskkill.exe /PID $process.Id /T /F 2>&1
                    $process.WaitForExit()
                }
            }
            $process.Dispose()
        }
        if (-not $ready) {
            $out = [System.IO.File]::ReadAllText($stdout)
            $errorText = [System.IO.File]::ReadAllText($stderr)
            throw ("Native QEMU boot failed ($case):`n$out`n$errorText")
        }
        Write-Host ("PASS real Windows QEMU boot to DESKTOP_READY: " + $case)
    }
    if ((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -ne $originalHash) {
        throw 'The original disk image was modified.'
    }
    if ((Get-FileHash -LiteralPath $renamedImage -Algorithm SHA256).Hash -ne $originalHash) {
        throw 'The Unicode-named disk image was modified.'
    }
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    if ([System.IO.Directory]::Exists($temporary)) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
