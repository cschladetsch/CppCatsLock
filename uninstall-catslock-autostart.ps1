[CmdletBinding()]
param(
    [string]$TaskName = 'Catslock',
    [string]$InstallDir = (Join-Path $env:ProgramFiles 'Catslock'),
    [switch]$RemoveFiles,
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

if ($RemoveFiles -and (Test-Path -LiteralPath $InstallDir)) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
    "Removed install directory '$InstallDir'"
}
