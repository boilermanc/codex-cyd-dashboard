param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Sweetwater/CrowesNest'),
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$taskName = 'Sweetwater Crowes Nest Helper'
$scheduler = New-Object -ComObject 'Schedule.Service'
$scheduler.Connect()
$folder = $scheduler.GetFolder('\')
if ($Remove) {
    $folder.DeleteTask($taskName, 0)
    Write-Output 'Helper recovery removed.'
    return
}
$executable = Join-Path (Resolve-Path -LiteralPath $InstallDirectory).Path 'CrowesNest.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    $executable = Join-Path (Resolve-Path -LiteralPath $InstallDirectory).Path 'CYD-USB-Helper.exe'
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'Extract the Windows helper before enabling recovery.'
}
$user = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$runner = Join-Path (Split-Path -Parent $executable) 'Watch-Helper.ps1'
$sourceRunner = Join-Path $PSScriptRoot 'Watch-Helper.ps1'
if ($sourceRunner -ne $runner) { Copy-Item -LiteralPath $sourceRunner -Destination $runner -Force }
$powershell = Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
$arguments = '-NoProfile -NonInteractive -WindowStyle Hidden -File "' + $runner + '" -InstallDirectory "' + (Split-Path -Parent $executable) + '"'
$command = [Security.SecurityElement]::Escape($powershell)
$xmlArguments = [Security.SecurityElement]::Escape($arguments)
$workingDirectory = [Security.SecurityElement]::Escape((Split-Path -Parent $executable))
$xml = @"
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo><Description>Start the CYD helper at sign-in and recover failed or hung workers.</Description></RegistrationInfo>
  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>$user</UserId></LogonTrigger></Triggers>
  <Principals><Principal id="User"><UserId>$user</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <StartWhenAvailable>true</StartWhenAvailable>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <RestartOnFailure><Interval>PT1M</Interval><Count>999</Count></RestartOnFailure>
  </Settings>
  <Actions Context="User"><Exec><Command>$command</Command><Arguments>$xmlArguments</Arguments><WorkingDirectory>$workingDirectory</WorkingDirectory></Exec></Actions>
</Task>
"@
foreach ($existing in $folder.GetTasks(0)) {
    if ($existing.Name -eq $taskName) {
        $action = $existing.Definition.Actions.Item(1)
        if ($action.Path -ne $executable -and ($action.Path -ne $powershell -or $action.Arguments -ne $arguments)) {
            throw 'A recovery task already targets another installation.'
        }
    }
}
$folder.RegisterTask($taskName, $xml, 6, $user, $null, 3, $null) | Out-Null
Write-Output 'Helper recovery enabled. Start the task to run it now.'
