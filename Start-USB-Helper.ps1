param([Parameter(Mandatory=$true)][string]$Port, [string]$DeviceId)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$localPath = Join-Path $taskRoot '.local'
New-Item -ItemType Directory -Force -Path $localPath | Out-Null
$pidFile = Join-Path $localPath 'usb-helper.pid'
if (Test-Path -LiteralPath $pidFile) {
 $existingId = [int](Get-Content -LiteralPath $pidFile)
 $existing = Get-CimInstance Win32_Process -Filter "ProcessId = $existingId" -ErrorAction SilentlyContinue
 if ($existing -and $existing.CommandLine -like ('*' + $taskRoot + '*usb-helper.py*')) { Write-Output 'USB helper already running.'; exit }
}
$stopFile = Join-Path $localPath 'usb-helper.stop'
if (Test-Path -LiteralPath $stopFile) { Remove-Item -LiteralPath $stopFile }
$python = Join-Path $taskRoot '.venv/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Create .venv and install requirements.txt first; see README.' }
if ($Port -notmatch '^COM[0-9]+$') { throw 'Port must be a Windows COM port.' }
if ($DeviceId -and $DeviceId -notmatch '^[a-fA-F0-9]{12}$') { throw 'DeviceId must be 12 hex digits.' }
$helperPath = Join-Path $taskRoot 'scripts/usb-helper.py'
$arguments = @('-u', ('"' + $helperPath + '"'), '--port', $Port)
if ($DeviceId) { $arguments += @('--device-id', $DeviceId) }
$process = Start-Process -FilePath $python -ArgumentList $arguments -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $localPath 'usb-helper.log') -RedirectStandardError (Join-Path $localPath 'usb-helper-error.log')
Set-Content -LiteralPath $pidFile -Value $process.Id
Write-Output "USB helper started (PID $($process.Id))."
