[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

& "$PSScriptRoot\b.ps1"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (Get-Process catslock -ErrorAction SilentlyContinue) {
    'Catslock already running'
    exit 0
}

Start-Process -Verb RunAs -FilePath "$PSScriptRoot\build-clang-uac\catslock.exe"
