@echo off
rem Launches Skyrim through MO2 + SKSE using the currently selected MO2 profile.
rem Set SKYCRAFT_MO2 to your Mod Organizer folder (the one with ModOrganizer.exe).
if "%SKYCRAFT_MO2%"=="" (
    echo Set SKYCRAFT_MO2 to your Mod Organizer folder first.
    exit /b 1
)
start "" "%SKYCRAFT_MO2%\ModOrganizer.exe" "moshortcut://Skyrim Special Edition:SKSE"
