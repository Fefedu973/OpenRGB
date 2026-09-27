@echo off
setlocal
if "%QT_ROOT%"=="" (echo Set QT_ROOT to the Qt MSVC directory. & exit /b 2)
where cl >nul 2>nul
if errorlevel 1 (echo Run from an x64 MSVC Developer Command Prompt. & exit /b 2)
if not exist "%~dp0.build" mkdir "%~dp0.build"
cd /d "%~dp0.build"
"%QT_ROOT%\bin\qmake.exe" ..\settings_ui_test.pro
if errorlevel 1 exit /b 1
nmake /nologo
if errorlevel 1 exit /b 1
set "PATH=%QT_ROOT%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT_ROOT%\plugins"
set "QT_QPA_PLATFORM=offscreen"
settings_ui_test.exe
exit /b %errorlevel%
