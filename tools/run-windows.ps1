# Windows PowerShell 5.1 compatible. Run only the prebuilt guest image, never a host disk.
param(
    [string]$QemuPath,
    [string]$ImagePath,
    [switch]$CheckOnly,
    [switch]$Headless
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

function Find-Xp96Image {
    param([string]$Requested, [string]$Root)

    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        $candidates = @($Requested)
    } else {
        $candidates = @(
            (Join-Path $Root 'xp96.img'),
            (Join-Path $Root 'build\xp96.img')
        )
    }
    foreach ($candidate in $candidates) {
        if (-not [System.IO.File]::Exists($candidate)) { continue }
        $file = Get-Item -LiteralPath $candidate
        $stream = [System.IO.File]::OpenRead($file.FullName)
        try {
            if ($stream.Length -lt 512) {
                throw 'Invalid disk image: the file is smaller than a BIOS boot sector.'
            }
            $stream.Position = 510
            if ($stream.ReadByte() -ne 0x55 -or $stream.ReadByte() -ne 0xaa) {
                throw 'Invalid disk image: the BIOS boot signature is missing.'
            }
        } finally {
            $stream.Dispose()
        }
        return $file.FullName
    }
    throw 'Disk image was not found. Download xp96-windows from Actions and extract the ENTIRE ZIP. See START-HERE.md.'
}

function Find-Xp96Qemu {
    param([string]$Requested)

    if ([string]::IsNullOrWhiteSpace($Requested)) { $Requested = $env:QEMU }
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        if ([System.IO.File]::Exists($Requested)) {
            return (Get-Item -LiteralPath $Requested).FullName
        }
        $command = Get-Command -Name $Requested -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) { return $command.Path }
        throw 'The specified QEMU executable does not exist. Check -QemuPath or the QEMU environment variable.'
    }

    $names = @('qemu-system-i386.exe', 'qemu-system-x86_64.exe')
    foreach ($name in $names) {
        $command = Get-Command -Name $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) { return $command.Path }
    }
    $folders = @($env:ProgramFiles, $env:ProgramW6432, ${env:ProgramFiles(x86)}) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
    foreach ($folder in $folders) {
        foreach ($name in $names) {
            $candidate = Join-Path (Join-Path $folder 'qemu') $name
            if ([System.IO.File]::Exists($candidate)) { return $candidate }
        }
    }
    throw 'QEMU was not found. Install QEMU for Windows from https://www.qemu.org/download/#windows or pass -QemuPath. See START-HERE.md.'
}

try {
    $root = Split-Path -Parent $PSScriptRoot
    $image = Find-Xp96Image -Requested $ImagePath -Root $root
    $qemu = Find-Xp96Qemu -Requested $QemuPath
    # Some Windows QEMU builds decode native argv using the system code page,
    # not UTF-8. Do not pass a Unicode absolute image path on the command line.
    # PowerShell sets the child's working directory using the Windows wide API.
    $workingDirectory = [System.IO.Path]::GetDirectoryName($image)
    $diskName = [System.IO.Path]::GetFileName($image)
    $stagingDirectory = $null
    if ($diskName -cmatch '[^\x20-\x7e]') {
        # Custom image names may also contain Unicode. A temporary ASCII-named
        # copy covers that case without renaming or modifying the original.
        $stagingDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('xp96-image-' + [guid]::NewGuid())
        $workingDirectory = $stagingDirectory
        $diskName = 'xp96.img'
    }
    # QEMU's -drive parser escapes a literal comma as two consecutive commas.
    $escapedName = $diskName.Replace(',', ',,')
    $qemuArguments = @(
        '-machine', 'pc', '-accel', 'tcg', '-cpu', 'qemu32',
        '-m', '64M', '-vga', 'std',
        '-drive', "file=./$escapedName,format=raw,if=ide",
        '-boot', 'c', '-snapshot', '-nic', 'none',
        '-serial', 'stdio', '-monitor', 'none'
    )
    if ($Headless) { $qemuArguments += @('-display', 'none') }
    if ($CheckOnly) {
        [ordered]@{
            Qemu = $qemu
            Image = $image
            WorkingDirectory = $workingDirectory
            StageImage = ($null -ne $stagingDirectory)
            Arguments = $qemuArguments
        } | ConvertTo-Json -Depth 3
        exit 0
    }
    Write-Host 'XP / 96 - starting your operating system' -ForegroundColor Cyan
    Write-Host 'Press Ctrl+Alt to release the mouse. Close the QEMU window to exit.'
    Write-Host 'RAM notes are not saved. Your Windows installation is not changed.'
    $locationChanged = $false
    try {
        if ($stagingDirectory) {
            $null = [System.IO.Directory]::CreateDirectory($stagingDirectory)
            [System.IO.File]::Copy($image, (Join-Path $stagingDirectory $diskName), $false)
        }
        Push-Location -LiteralPath $workingDirectory
        $locationChanged = $true
        & $qemu @qemuArguments
        $qemuExit = $LASTEXITCODE
    } finally {
        if ($locationChanged) { Pop-Location }
        if ($stagingDirectory -and [System.IO.Directory]::Exists($stagingDirectory)) {
            [System.IO.Directory]::Delete($stagingDirectory, $true)
        }
    }
    exit $qemuExit
} catch {
    [Console]::Error.WriteLine('XP / 96: ' + $_.Exception.Message)
    exit 1
}
