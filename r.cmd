@echo off
setlocal

call "%~dp0b.cmd" || exit /b %ERRORLEVEL%
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "if (Get-Process catslock -ErrorAction SilentlyContinue) { 'Catslock already running'; exit 0 }; Start-Process -Verb RunAs -FilePath '%~dp0build-clang-uac\catslock.exe'"
