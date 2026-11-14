# Windows process/argument regression checks. Real boot is also covered by windows-qemu.ps1.
param([Parameter(Mandatory = $true)][string]$PackagePath)
$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}
function Quote-Argument {
    param([string]$Value)
    return ('"' + $Value + '"')
}
function Invoke-Captured {
    param([string]$Executable, [string]$ArgumentString)
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.Arguments = $ArgumentString
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [System.Text.UTF8Encoding]::new($false)
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        $null = $process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        return @{ Code = $process.ExitCode; Out = $stdout.Result; Error = $stderr.Result }
    } finally {
        $process.Dispose()
    }
}
function Invoke-Script {
    param([string[]]$Parameters = @())
    $arguments = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script:launcherScript) + $Parameters
    $argumentString = ($arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
    return Invoke-Captured -Executable "$PSHOME\powershell.exe" -ArgumentString $argumentString
}
function Invoke-Batch {
    return Invoke-Captured -Executable "$env:WINDIR\System32\cmd.exe" -ArgumentString ('/d /s /c ""' + $script:batch + '""')
}

$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ('xp96-test-' + [guid]::NewGuid())
$environmentNames = @('PATH', 'QEMU', 'ProgramFiles', 'ProgramW6432', 'ProgramFiles(x86)', 'XP96_NO_PAUSE', 'XP96_TEST_ARGS', 'XP96_TEST_CWD', 'XP96_TEST_EXIT')
$oldEnvironment = @{}
foreach ($name in $environmentNames) { $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }

try {
    # "Test" in Cyrillic, constructed as code points so Windows PowerShell 5.1 can
    # read this source even if an editor has saved it as UTF-8 without a BOM.
    $unicode = -join ([char[]](0x0422, 0x0435, 0x0441, 0x0442))
    $root = Join-Path $temporary ('Windows, [test] & ' + $unicode + ' !')
    $null = New-Item -ItemType Directory -Path $root
    $null = New-Item -ItemType Directory -Path (Join-Path $root 'tools')
    foreach ($name in @('Start-XP96.bat', 'xp96.img', 'tools\run-windows.ps1')) {
        Copy-Item -LiteralPath (Join-Path $PackagePath $name) -Destination (Join-Path $root $name)
    }
    $script:launcherScript = Join-Path $root 'tools\run-windows.ps1'
    $script:batch = Join-Path $root 'Start-XP96.bat'
    $image = Join-Path $root 'xp96.img'
    $stubDirectory = Join-Path $temporary 'QEMU with spaces'
    $null = New-Item -ItemType Directory -Path $stubDirectory
    $stub = Join-Path $stubDirectory 'qemu-system-i386.exe'
    Add-Type -OutputAssembly $stub -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Text;
public class Xp96QemuStub {
    public static int Main(string[] args) {
        File.WriteAllLines(Environment.GetEnvironmentVariable("XP96_TEST_ARGS"), args, new UTF8Encoding(false));
        File.WriteAllText(Environment.GetEnvironmentVariable("XP96_TEST_CWD"), Environment.CurrentDirectory, new UTF8Encoding(false));
        int driveIndex = Array.IndexOf(args, "-drive");
        if (driveIndex < 0 || driveIndex + 1 >= args.Length) return 80;
        string drive = args[driveIndex + 1];
        if (!drive.StartsWith("file=./")) return 81;
        foreach (char c in drive) if (c > 127) return 82;
        const string suffix = ",format=raw,if=ide";
        if (!drive.EndsWith(suffix)) return 83;
        string image = drive.Substring(5, drive.Length - 5 - suffix.Length).Replace(",,", ",");
        if (!File.Exists(image)) return 84;
        byte[] header = File.ReadAllBytes(image);
        if (header.Length < 512 || header[510] != 0x55 || header[511] != 0xaa) return 85;
        // Simulate guest writes: these must affect only the disposable copy.
        using (FileStream disk = File.Open(image, FileMode.Open, FileAccess.Write)) disk.WriteByte(0x90);
        int code;
        return int.TryParse(Environment.GetEnvironmentVariable("XP96_TEST_EXIT"), out code) ? code : 0;
    }
}
'@
    $env:XP96_NO_PAUSE = '1'
    $env:XP96_TEST_ARGS = Join-Path $temporary 'arguments.txt'
    $env:XP96_TEST_CWD = Join-Path $temporary 'working-directory.txt'
    $env:XP96_TEST_EXIT = '0'
    $env:QEMU = $stub

    $result = Invoke-Script -Parameters @('-QemuPath', $stub, '-CheckOnly')
    Assert-True ($result.Code -eq 0) ("CheckOnly failed: " + $result.Error)
    $plan = $result.Out | ConvertFrom-Json
    Assert-True ($plan.Image -eq $image) 'Image must be resolved relative to the launcher, not the working directory.'
    Assert-True ($plan.Qemu -eq $stub) 'The explicitly selected executable must be used.'
    $drive = 'file=./xp96.img,format=raw,if=ide'
    Assert-True ($plan.StageImage -eq $true) 'Always protect the original disk with a private raw copy.'
    Assert-True (-not (Test-Path -LiteralPath $plan.WorkingDirectory)) 'Planning must not create the private run directory.'
    Assert-True ($plan.Arguments -contains $drive) 'Pass an ASCII relative filename, not a Unicode absolute path.'
    foreach ($argument in @('tcg', '64M', '-nic', 'none')) {
        Assert-True ($plan.Arguments -contains $argument) ("Missing safe launch argument: " + $argument)
    }
    Assert-True (-not ($plan.Arguments -contains '-snapshot')) 'Do not let QEMU create a broken Unicode TEMP overlay.'
    Assert-True (-not (Test-Path -LiteralPath $env:XP96_TEST_ARGS)) 'CheckOnly must not execute QEMU.'
    Write-Host 'PASS PowerShell command planning, safe defaults and unusual paths'

    $sourceHash = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
    $result = Invoke-Batch
    Assert-True ($result.Code -eq 0) ("Batch launch failed: " + $result.Error + $result.Out)
    $received = @(Get-Content -LiteralPath $env:XP96_TEST_ARGS -Encoding UTF8)
    Assert-True (($received -join "`n") -eq ($plan.Arguments -join "`n")) 'The batch launcher must preserve every QEMU argument.'
    $env:XP96_TEST_EXIT = '7'
    $result = Invoke-Batch
    Assert-True ($result.Code -eq 7) 'The QEMU failure code must reach the caller.'
    $env:XP96_TEST_EXIT = '0'
    $runDirectory = Get-Content -LiteralPath $env:XP96_TEST_CWD -Raw -Encoding UTF8
    Assert-True (-not (Test-Path -LiteralPath $runDirectory)) 'Clean up the private copy when QEMU exits.'
    Assert-True ((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -eq $sourceHash) 'Writes to the emulated disk must not change the original.'
    Write-Host 'PASS double-click batch entry point, relative file opening and error propagation'

    $copy = Join-Path $root 'copy, [with space].img'
    Copy-Item -LiteralPath $image -Destination $copy
    $result = Invoke-Script -Parameters @('-ImagePath', $copy, '-CheckOnly', '-Headless')
    Assert-True ($result.Code -eq 0) ("Custom image planning failed: " + $result.Error)
    $copyPlan = $result.Out | ConvertFrom-Json
    Assert-True ($copyPlan.Arguments -contains 'file=./xp96.img,format=raw,if=ide') 'Do not pass spaces or commas from a custom source name to native argv.'
    Assert-True ($copyPlan.Arguments -contains '-display') 'Headless mode must request no display.'
    $result = Invoke-Script -Parameters @('-ImagePath', $copy, '-Headless')
    Assert-True ($result.Code -eq 0) ("Relative custom filename failed: " + $result.Error)
    Write-Host 'PASS source filename with spaces/commas and headless mode'

    $unicodeImage = Join-Path $root ($unicode + '.img')
    Copy-Item -LiteralPath $image -Destination $unicodeImage
    $before = (Get-FileHash -LiteralPath $unicodeImage -Algorithm SHA256).Hash
    $result = Invoke-Script -Parameters @('-ImagePath', $unicodeImage, '-CheckOnly')
    Assert-True ($result.Code -eq 0) ("Unicode filename planning failed: " + $result.Error)
    $unicodePlan = $result.Out | ConvertFrom-Json
    Assert-True ($unicodePlan.StageImage -eq $true) 'Stage a Unicode basename under an ASCII filename.'
    Assert-True (-not (Test-Path -LiteralPath $unicodePlan.WorkingDirectory)) 'CheckOnly must not create a staging directory.'
    $result = Invoke-Script -Parameters @('-ImagePath', $unicodeImage)
    Assert-True ($result.Code -eq 0) ("Unicode filename launch failed: " + $result.Error)
    $staged = Get-Content -LiteralPath $env:XP96_TEST_CWD -Raw -Encoding UTF8
    Assert-True (-not (Test-Path -LiteralPath $staged)) 'Clean up the staged copy after successful exit.'
    $env:XP96_TEST_EXIT = '7'
    $result = Invoke-Script -Parameters @('-ImagePath', $unicodeImage)
    Assert-True ($result.Code -eq 7) 'Preserve QEMU exit codes when staging.'
    $staged = Get-Content -LiteralPath $env:XP96_TEST_CWD -Raw -Encoding UTF8
    Assert-True (-not (Test-Path -LiteralPath $staged)) 'Also clean up staging on QEMU failure.'
    $env:XP96_TEST_EXIT = '0'
    Assert-True ((Get-FileHash -LiteralPath $unicodeImage -Algorithm SHA256).Hash -eq $before) 'Never modify or rename the original image.'
    Write-Host 'PASS Unicode image basename, side-effect-free check and staging cleanup'

    $env:QEMU = ''
    $env:PATH = $stubDirectory + ';' + $oldEnvironment['PATH']
    $result = Invoke-Script -Parameters @('-CheckOnly')
    Assert-True ($result.Code -eq 0) ("PATH discovery failed: " + $result.Error)
    Assert-True (($result.Out | ConvertFrom-Json).Qemu -eq $stub) 'Find the installed QEMU on PATH.'
    Write-Host 'PASS QEMU detection on PATH'

    $programFiles = Join-Path $temporary 'Custom Program Files'
    $qemuFolder = Join-Path $programFiles 'qemu'
    $null = New-Item -ItemType Directory -Path $qemuFolder -Force
    $fallback = Join-Path $qemuFolder 'qemu-system-x86_64.exe'
    Copy-Item -LiteralPath $stub -Destination $fallback
    $env:PATH = "$env:WINDIR\System32;$env:WINDIR\System32\WindowsPowerShell\v1.0"
    $env:ProgramFiles = $programFiles
    $env:ProgramW6432 = $programFiles
    ${env:ProgramFiles(x86)} = $programFiles
    $result = Invoke-Script -Parameters @('-CheckOnly')
    Assert-True ($result.Code -eq 0) ("Program Files discovery failed: " + $result.Error)
    Assert-True (($result.Out | ConvertFrom-Json).Qemu -eq $fallback) 'Support the standard install folder and x86_64 executable.'
    $env:QEMU = $stub
    Write-Host 'PASS Program Files detection and 64-bit QEMU fallback'

    $build = Join-Path $root 'build'
    $null = New-Item -ItemType Directory -Path $build
    Move-Item -LiteralPath $image -Destination (Join-Path $build 'xp96.img')
    $result = Invoke-Script -Parameters @('-CheckOnly')
    Assert-True ($result.Code -eq 0) ("Build image fallback failed: " + $result.Error)
    Assert-True (($result.Out | ConvertFrom-Json).Image -eq (Join-Path $build 'xp96.img')) 'Also support an image built from source.'
    [System.IO.File]::WriteAllBytes($image, [byte[]]::new(512))
    $result = Invoke-Script -Parameters @('-CheckOnly')
    Assert-True ($result.Code -ne 0 -and $result.Error.Contains('boot signature')) 'Reject a corrupt boot sector.'
    Remove-Item -LiteralPath $image
    $result = Invoke-Script -Parameters @('-ImagePath', (Join-Path $root 'missing.img'), '-CheckOnly')
    Assert-True ($result.Code -ne 0 -and $result.Error.Contains('Disk image was not found')) 'Explain a missing explicitly selected image.'
    $result = Invoke-Script -Parameters @('-QemuPath', (Join-Path $root 'missing.exe'), '-CheckOnly')
    Assert-True ($result.Code -ne 0 -and $result.Error.Contains('specified QEMU')) 'Never silently replace an invalid explicit QEMU selection.'
    Write-Host 'PASS source checkout fallback, corrupt image and missing-file diagnostics'

    Remove-Item -LiteralPath $script:launcherScript
    $result = Invoke-Batch
    Assert-True ($result.Code -ne 0 -and $result.Out.Contains('launcher files are missing')) 'Explain an incompletely extracted archive.'
    Write-Host 'PASS incomplete ZIP extraction diagnostic'
} finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process') }
    if ([System.IO.Directory]::Exists($temporary)) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
