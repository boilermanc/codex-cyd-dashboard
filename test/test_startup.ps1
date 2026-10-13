# Integration test: create a real Windows shortcut in a temporary folder,
# redirecting Startup writes so this never changes the user's startup entries.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1')
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Management\Microsoft.PowerShell.Management.psd1')
$global:cydTest_sourceRoot = Split-Path $PSScriptRoot
$global:cydTest_startupScript = Join-Path $global:cydTest_sourceRoot 'Set-USB-HelperStartup.ps1'
$global:cydTest_fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('cyd-startup-test-' + [guid]::NewGuid())
Microsoft.PowerShell.Management\New-Item -ItemType Directory -Path $global:cydTest_fixtureRoot | Out-Null
$global:cydTest_fixtureShortcut = Join-Path $global:cydTest_fixtureRoot 'helper.lnk'
$global:cydTest_realShell = Microsoft.PowerShell.Utility\New-Object -ComObject WScript.Shell
$global:cydTest_requestedShortcut = $null
function Assert($Condition, $Message) { if (-not $Condition) { throw $Message } }
function Test-Path {
    param([string]$LiteralPath)
    if ($LiteralPath -eq (Join-Path $global:cydTest_sourceRoot '.venv\Scripts\python.exe')) { return $true }
    if ($LiteralPath -eq $global:cydTest_requestedShortcut) { return Microsoft.PowerShell.Management\Test-Path -LiteralPath $global:cydTest_fixtureShortcut }
    Microsoft.PowerShell.Management\Test-Path -LiteralPath $LiteralPath
}
function New-Item { param($ItemType, [switch]$Force, $Path) }
function New-Object {
    param($ComObject)
    Assert ($ComObject -eq 'WScript.Shell') 'Unexpected COM object.'
    $proxy = [pscustomobject]@{}
    $proxy | Add-Member ScriptMethod CreateShortcut {
        param($Path)
        $global:cydTest_requestedShortcut = $Path
        $global:cydTest_realShell.CreateShortcut($global:cydTest_fixtureShortcut)
    }
    $proxy
}
function Remove-Item {
    param([string]$LiteralPath)
    Assert ($LiteralPath -eq $global:cydTest_requestedShortcut) 'Removal targeted another installation.'
    Microsoft.PowerShell.Management\Remove-Item -LiteralPath $global:cydTest_fixtureShortcut
}
try {
    & $global:cydTest_startupScript -Port COM11 -DeviceId aabbccddeeff
    $link = $global:cydTest_realShell.CreateShortcut($global:cydTest_fixtureShortcut)
    Assert ($link.TargetPath -like '*\powershell.exe') 'Wrong startup executable.'
    Assert ($link.Arguments -like '*-File "*Start-USB-Helper.ps1" -Port COM11 -DeviceId aabbccddeeff') 'Launcher arguments lost port, identity or quoting.'
    Assert ($link.WorkingDirectory -eq $global:cydTest_sourceRoot) 'Wrong working directory.'
    & $global:cydTest_startupScript -Port COM9
    $link = $global:cydTest_realShell.CreateShortcut($global:cydTest_fixtureShortcut)
    Assert ($link.Arguments -like '*-Port COM9') 'Reinstall did not update the port.'
    Assert ($link.Arguments -notlike '*DeviceId*') 'Reinstall retained an unwanted identity.'
    & $global:cydTest_startupScript -Remove -WhatIf
    Assert (Microsoft.PowerShell.Management\Test-Path -LiteralPath $global:cydTest_fixtureShortcut) 'WhatIf removed the shortcut.'
    & $global:cydTest_startupScript -Remove
    Assert (-not (Microsoft.PowerShell.Management\Test-Path -LiteralPath $global:cydTest_fixtureShortcut)) 'Removal failed.'
    & $global:cydTest_startupScript -Port COM11 -WhatIf
    Assert (-not (Microsoft.PowerShell.Management\Test-Path -LiteralPath $global:cydTest_fixtureShortcut)) 'WhatIf installed a shortcut.'
    foreach ($parameters in @(@{}, @{Port='COM11;calc'}, @{Port='COM11';DeviceId='invalid'})) {
        $rejected = $false
        try { & $global:cydTest_startupScript @parameters } catch { $rejected = $true }
        Assert $rejected 'Invalid startup parameters were accepted.'
    }
    Write-Output 'Startup shortcut tests passed.'
} finally {
    Microsoft.PowerShell.Management\Remove-Item -LiteralPath $global:cydTest_fixtureRoot -Recurse -Force
}
