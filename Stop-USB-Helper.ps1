$ErrorActionPreference = 'Stop'
$localPath = Join-Path $PSScriptRoot '.local'
if (Test-Path -LiteralPath $localPath) { Set-Content -LiteralPath (Join-Path $localPath 'usb-helper.stop') -Value 'stop' }
Write-Output 'Stop requested; helper releases its serial port.'
