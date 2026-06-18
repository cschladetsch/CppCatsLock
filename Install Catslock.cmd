@echo off
setlocal

set "SCRIPT=%~dp0install-catslock-gui.ps1"

if not exist "%SCRIPT%" (
    echo Could not find install-catslock-gui.ps1 next to this launcher.
    echo Extract the full Catslock release zip and run this file from that folder.
    pause
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%SCRIPT%"
if errorlevel 1 (
    echo.
    echo Catslock installer exited with an error.
    pause
    exit /b %errorlevel%
)
