@echo off
setlocal DisableDelayedExpansion
title XP / 96 - Windows launcher

if not exist "%~dp0tools\run-windows.ps1" (
    echo XP / 96: launcher files are missing.
    echo Extract the ENTIRE archive first, then run Start-XP96.bat again.
    if not defined XP96_NO_PAUSE pause
    exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run-windows.ps1" %*
set "XP96_EXIT=%ERRORLEVEL%"

if not "%XP96_EXIT%"=="0" (
    echo.
    echo See START-HERE.md or docs\WINDOWS.md for help.
    if not defined XP96_NO_PAUSE pause
)
exit /b %XP96_EXIT%
