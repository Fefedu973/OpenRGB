@echo off
setlocal
set "ROOT=%~dp0..\.."
set "OUT=%~dp0.build"
if not exist "%OUT%" mkdir "%OUT%"
if not defined VSCMD_VER call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 2
cl /nologo /EHsc /std:c++17 /MD /W3 /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /I"%ROOT%" /I"%ROOT%\RGBController" /I"%ROOT%\dependencies\json" /I"%ROOT%\net_port" /I"%ROOT%\Controllers\GoveeController" "%~dp0test_discovery.cpp" "%ROOT%\Controllers\GoveeController\GoveeController.cpp" "%ROOT%\net_port\net_port.cpp" /Fo"%OUT%\\" /Fe"%OUT%\test_discovery.exe" /link ws2_32.lib
if errorlevel 1 exit /b 1
"%OUT%\test_discovery.exe"
exit /b %errorlevel%
