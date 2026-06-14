#!/usr/bin/env sh
set -eu

"$(dirname "$0")/b"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "if (Get-Process catslock -ErrorAction SilentlyContinue) { 'Catslock already running'; exit 0 }; Start-Process -Verb RunAs -FilePath './build-clang-uac/catslock.exe'"
