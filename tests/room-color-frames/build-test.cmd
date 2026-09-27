@echo off
setlocal
if "%QT_ROOT%"=="" (echo Set QT_ROOT to a Qt MSVC installation. & exit /b 2)
where cl >nul 2>nul
if errorlevel 1 (echo Use an x64 MSVC Developer Command Prompt. & exit /b 2)
if not exist "%~dp0.build" mkdir "%~dp0.build"
cd /d "%~dp0.build"
"%QT_ROOT%\bin\qmake.exe" ..\color_frames.pro
if errorlevel 1 exit /b 1
nmake /nologo
if errorlevel 1 exit /b 1
set "PATH=%QT_ROOT%\bin;%PATH%"
color_frames_test.exe
exit /b %errorlevel%
