param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Sweetwater/CrowesNest')
)
$ErrorActionPreference = 'Stop'
$executable = Join-Path $InstallDirectory 'CrowesNest.exe'
$arguments = @('--run-helper')
if (-not (Test-Path -LiteralPath $executable)) {
    $executable = Join-Path $InstallDirectory 'CYD-USB-Helper.exe'
    $config = Get-Content -LiteralPath (Join-Path $InstallDirectory 'connection.json') -Raw | ConvertFrom-Json
    if ($config.port -notmatch '^COM[0-9]+$' -or $config.device_id -notmatch '^[a-fA-F0-9]{12}$') {
        throw 'Configure the CYD port and 12-digit board identity in connection.json first.'
    }
    $arguments = @('--port', $config.port, '--device-id', $config.device_id)
}
$localDirectory = Join-Path $InstallDirectory '.local'
New-Item -ItemType Directory -Path $localDirectory -Force | Out-Null
try {
    $lock = [IO.File]::Open((Join-Path $localDirectory 'supervisor.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
} catch [IO.IOException] {
    exit 0
}
try {
    $stopFile = Join-Path $localDirectory 'usb-helper.stop'
    if (Test-Path -LiteralPath $stopFile) { Remove-Item -LiteralPath $stopFile }
    do {
        $worker = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $InstallDirectory -WindowStyle Hidden -PassThru
        $null = $worker.Handle
        $worker.WaitForExit()
        $exitCode = $worker.ExitCode
        $worker.Dispose()
        if ($exitCode -eq 0 -or (Test-Path -LiteralPath (Join-Path $localDirectory 'usb-helper.stop'))) {
            break
        }
        $event = @{timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds(); event = 'worker_exit'; exit_code = $exitCode}
        $line = $event | ConvertTo-Json -Compress
        try {
            Set-Content -LiteralPath (Join-Path $localDirectory 'supervisor-last-restart.json') -Value $line -Encoding UTF8
        } catch {
            # Recovery continues even when the diagnostic file is temporarily locked.
        }
        Start-Sleep -Seconds 2
    } while ($true)
} finally {
    $lock.Dispose()
}
