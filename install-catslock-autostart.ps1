[CmdletBinding()]
param(
    [string]$TaskName = 'Catslock',
    [string]$ExePath,
    [switch]$NoStart
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-CatslockExe {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        $resolved = Resolve-Path -LiteralPath $RequestedPath -ErrorAction Stop
        return $resolved.ProviderPath
    }

    $candidates = @(
        (Join-Path $PSScriptRoot 'catslock.exe'),
        (Join-Path $PSScriptRoot 'build-clang-uac\catslock.exe'),
        (Join-Path $PSScriptRoot 'build-clang\catslock.exe'),
        (Join-Path $PSScriptRoot 'build\Release\catslock.exe'),
        (Join-Path $PSScriptRoot 'build\catslock.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).ProviderPath
        }
    }

    throw 'Could not find catslock.exe. Build Catslock first or pass -ExePath.'
}

$catslockExe = Resolve-CatslockExe -RequestedPath $ExePath
$workingDirectory = Split-Path -Parent $catslockExe
$currentIdentity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name

$action = New-ScheduledTaskAction -Execute $catslockExe -WorkingDirectory $workingDirectory
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $currentIdentity
$principal = New-ScheduledTaskPrincipal -UserId $currentIdentity -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Days 365) `
    -MultipleInstances IgnoreNew `
    -RestartCount 3 `
    -RestartInterval (New-TimeSpan -Minutes 1)

Register-ScheduledTask `
    -TaskName $TaskName `
    -Action $action `
    -Trigger $trigger `
    -Principal $principal `
    -Settings $settings `
    -Description 'Starts Catslock in the interactive user session at logon.' `
    -Force | Out-Null

if (-not $NoStart) {
    Start-ScheduledTask -TaskName $TaskName
}

"Installed scheduled task '$TaskName' for $catslockExe"
