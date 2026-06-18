[CmdletBinding()]
param(
    [string]$TaskName = 'Catslock',
    [string]$ExePath,
    [string]$InstallDir = (Join-Path $env:ProgramFiles 'Catslock'),
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

$sourceCatslockExe = Resolve-CatslockExe -RequestedPath $ExePath
$resolvedInstallDir = (New-Item -ItemType Directory -Path $InstallDir -Force).FullName
$catslockExe = Join-Path $resolvedInstallDir 'catslock.exe'

if (-not [StringComparer]::OrdinalIgnoreCase.Equals($sourceCatslockExe, $catslockExe)) {
    Copy-Item -LiteralPath $sourceCatslockExe -Destination $catslockExe -Force
}

foreach ($supportFile in @(
    'catslock.ps1',
    'Install Catslock.cmd',
    'install-catslock-autostart.ps1',
    'install-catslock-gui.ps1',
    'uninstall-catslock-autostart.ps1',
    'Readme.md'
)) {
    $sourcePath = Join-Path $PSScriptRoot $supportFile
    if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
        $destinationPath = Join-Path $resolvedInstallDir $supportFile
        $resolvedSourcePath = (Resolve-Path -LiteralPath $sourcePath).ProviderPath
        if (-not [StringComparer]::OrdinalIgnoreCase.Equals($resolvedSourcePath, $destinationPath)) {
            Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        }
    }
}

$workingDirectory = $resolvedInstallDir
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
