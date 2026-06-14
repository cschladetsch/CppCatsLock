[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

cmake -S . -B build-clang-uac -G Ninja -DCMAKE_CXX_COMPILER=clang++
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build build-clang-uac
exit $LASTEXITCODE
