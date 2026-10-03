@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0scripts\setup.ps1"
set "launch_result=%errorlevel%"
echo.
if not "%launch_result%"=="0" echo The script reported an error. Copy the message above before closing this window.
pause
exit /b %launch_result%
