@echo off
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0tools\room-setup\Start-Room.ps1"
exit /b %errorlevel%
