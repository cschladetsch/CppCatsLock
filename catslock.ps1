[CmdletBinding()]
param(
    [switch]$Toggle
)

$statePath = Join-Path ([System.IO.Path]::GetTempPath()) 'catslock.state'
$eventName = 'CatslockToggle'

function Get-CatslockIsOn {
    if (-not (Test-Path -LiteralPath $statePath)) {
        return $false
    }

    $state = (Get-Content -LiteralPath $statePath -Raw).Trim()
    return $state -eq 'CATSLOCK_ON' -or $state -eq 'FIRST_TAP_OFF' -or $state -eq 'ON'
}

function Write-CatslockState {
    param([bool]$Enabled)

    $state = if ($Enabled) { 'CATSLOCK_ON' } else { 'CATSLOCK_OFF' }
    Set-Content -LiteralPath $statePath -Value $state -NoNewline -Encoding ascii
}

function Signal-CatslockToggle {
    $source = @'
using System;
using System.Runtime.InteropServices;

public static class CatslockNativeMethods
{
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr OpenEvent(uint desiredAccess, bool inheritHandle, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool SetEvent(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

    if (-not ('CatslockNativeMethods' -as [type])) {
        Add-Type -TypeDefinition $source
    }

    $eventModifyState = 0x0002
    $handle = [CatslockNativeMethods]::OpenEvent($eventModifyState, $false, $eventName)
    if ($handle -eq [IntPtr]::Zero) {
        Write-Warning 'CatslockToggle event is not available. Start catslock.exe before using -Toggle.'
        return
    }

    try {
        [void][CatslockNativeMethods]::SetEvent($handle)
    }
    finally {
        [void][CatslockNativeMethods]::CloseHandle($handle)
    }
}

if ($Toggle) {
    Write-CatslockState -Enabled:(-not (Get-CatslockIsOn))
    Signal-CatslockToggle
}

if (Get-CatslockIsOn) {
    'Catslock: ON'
} else {
    'Catslock: OFF'
}
