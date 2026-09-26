@echo off
setlocal
set "ROOT=%~dp0..\.."
set "BUILD=%~dp0.build"
if not exist "%BUILD%" mkdir "%BUILD%"
if "%VCVARS%"=="" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" exit /b 2
call "%VCVARS%" >nul
if errorlevel 1 exit /b 2
cl /nologo /EHsc /std:c++17 /MD /O2 /W4 /DNOMINMAX /I"%ROOT%" "%~dp0test_surface.cpp" /Fo"%BUILD%\\" /Fe"%BUILD%\test_surface.exe"
if errorlevel 1 exit /b 1
"%BUILD%\test_surface.exe"
exit /b %errorlevel%
