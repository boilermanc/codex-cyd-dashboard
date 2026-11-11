$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$directory = Join-Path $env:TEMP ('cyd-recovery-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null
$previousStop = $env:CYD_RECOVERY_TEST_STOP
function Run-Supervisor {
    & powershell.exe -NoProfile -NonInteractive -File (Join-Path $repo 'Watch-Helper.ps1') -InstallDirectory $directory
    if ($LASTEXITCODE) { throw 'Supervisor failed.' }
}
function Assert-Count($expected) {
    if ([int](Get-Content (Join-Path $directory 'worker-count.txt')) -ne $expected) {
        throw 'Unexpected number of worker launches.'
    }
}
try {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
public class Worker {
    public static int Main(string[] args) {
        string file = "worker-count.txt";
        int count = File.Exists(file) ? int.Parse(File.ReadAllText(file)) + 1 : 1;
        File.WriteAllText(file, count.ToString());
        File.WriteAllText("worker-args.txt", string.Join(" ", args));
        if (Environment.GetEnvironmentVariable("CYD_RECOVERY_TEST_STOP") == "1") {
            Directory.CreateDirectory(".local");
            File.WriteAllText(Path.Combine(".local", "usb-helper.stop"), "stop");
        }
        return count == 1 ? 70 : 0;
    }
}
'@ -OutputAssembly (Join-Path $directory 'CrowesNest.exe') -OutputType ConsoleApplication
    $env:CYD_RECOVERY_TEST_STOP = ''
    Run-Supervisor
    Assert-Count 2
    $event = Get-Content (Join-Path $directory '.local/supervisor-last-restart.json') -Raw | ConvertFrom-Json
    if ($event.exit_code -ne 70) { throw 'Worker failure was not recorded.' }
    Remove-Item -LiteralPath (Join-Path $directory 'worker-count.txt')
    $env:CYD_RECOVERY_TEST_STOP = '1'
    Run-Supervisor
    Assert-Count 1
    $env:CYD_RECOVERY_TEST_STOP = ''
    Remove-Item -LiteralPath (Join-Path $directory 'worker-count.txt')
    Move-Item -LiteralPath (Join-Path $directory 'CrowesNest.exe') -Destination (Join-Path $directory 'CYD-USB-Helper.exe')
    @{port='COM11'; device_id='0123456789ab'} | ConvertTo-Json | Set-Content (Join-Path $directory 'connection.json') -Encoding ASCII
    Run-Supervisor
    Assert-Count 2
    if ((Get-Content (Join-Path $directory 'worker-args.txt')) -ne '--port COM11 --device-id 0123456789ab') {
        throw 'Portable board configuration was not forwarded correctly.'
    }
    $lock = [IO.File]::Open((Join-Path $directory '.local/supervisor.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    try {
        Run-Supervisor
        Assert-Count 2
    } finally { $lock.Dispose() }
    Write-Output 'Recovery supervisor tests passed.'
} finally {
    $env:CYD_RECOVERY_TEST_STOP = $previousStop
    $resolved = [IO.Path]::GetFullPath($directory)
    $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Test cleanup path is outside TEMP.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
