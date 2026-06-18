[CmdletBinding()]
param(
    [string]$TaskName = 'Catslock',
    [switch]$StopProcess
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($task) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    "Removed scheduled task '$TaskName'"
} else {
    "Scheduled task '$TaskName' was not installed"
}

if ($StopProcess) {
    Get-Process catslock -ErrorAction SilentlyContinue | Stop-Process -Force
    'Stopped running catslock.exe processes'
}
