[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [ValidatePattern('^COM[0-9]+$')][string]$Port,
    [ValidatePattern('^[a-fA-F0-9]{12}$')][string]$DeviceId,
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Windows startup integration requires Windows.' }
$startupFolder = [Environment]::GetFolderPath('Startup')
if (-not $startupFolder) { throw 'The current user has no Windows Startup folder.' }
# Each checkout has its own shortcut; do not overwrite another installation.
$hash = [System.Security.Cryptography.SHA256]::Create()
try {
    $bytes = [Text.Encoding]::UTF8.GetBytes($PSScriptRoot.ToLowerInvariant())
    $installationId = ([BitConverter]::ToString($hash.ComputeHash($bytes))).Replace('-', '').Substring(0, 12)
} finally { $hash.Dispose() }
$shortcutPath = Join-Path $startupFolder ('Codex CYD USB Helper-' + $installationId + '.lnk')
if ($Remove) {
    if (Test-Path -LiteralPath $shortcutPath) {
        if ($PSCmdlet.ShouldProcess($shortcutPath, 'Remove CYD sign-in startup shortcut')) {
            Remove-Item -LiteralPath $shortcutPath
            Write-Output 'Automatic startup removed. The running helper is unchanged.'
        }
    } else { Write-Output 'Automatic startup is not installed for this checkout.' }
    return
}
if (-not $Port) { throw 'Specify -Port with the CYD port, for example COM11.' }
$launcher = Join-Path $PSScriptRoot 'Start-USB-Helper.ps1'
$python = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $launcher)) { throw 'Start-USB-Helper.ps1 is missing.' }
if (-not (Test-Path -LiteralPath $python)) { throw 'Create .venv and install requirements.txt first; see README.' }
$powershell = Join-Path ([Environment]::GetFolderPath('Windows')) 'System32\WindowsPowerShell\v1.0\powershell.exe'
$arguments = '-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + $launcher + '" -Port ' + $Port
if ($DeviceId) { $arguments += ' -DeviceId ' + $DeviceId }
if ($PSCmdlet.ShouldProcess($shortcutPath, 'Install CYD helper startup at Windows sign-in')) {
    New-Item -ItemType Directory -Force -Path $startupFolder | Out-Null
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $powershell
    $shortcut.Arguments = $arguments
    $shortcut.WorkingDirectory = $PSScriptRoot
    $shortcut.Description = 'Start the Codex CYD USB helper on ' + $Port
    $shortcut.WindowStyle = 7
    $shortcut.Save()
    Write-Output ('Automatic startup installed for ' + $Port + '. It starts at your next Windows sign-in; use Start-USB-Helper.ps1 to start now.')
}
