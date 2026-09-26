@echo off
setlocal
if "%QT_ROOT%"=="" exit /b 2
set "ROOT=%~dp0"
if not exist "%ROOT%.build" mkdir "%ROOT%.build"
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 2
cd /d "%ROOT%.build"
"%QT_ROOT%\bin\qmake.exe" "%ROOT%plugin_tests.pro"
if errorlevel 1 exit /b 1
nmake /nologo
exit /b %errorlevel%
