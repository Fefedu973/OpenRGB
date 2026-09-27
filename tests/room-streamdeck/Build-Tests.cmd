@echo off
setlocal
set "ROOT=%~dp0..\.."
set "BUILD=%~dp0.build"
if not exist "%BUILD%" mkdir "%BUILD%"
if "%VCVARS%"=="" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" exit /b 2
call "%VCVARS%" >nul
if errorlevel 1 exit /b 2
cl /nologo /EHsc /std:c++17 /MD /W4 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I"%ROOT%\dependencies\json" /I"%ROOT%\dependencies\httplib" /I"%ROOT%\Controllers\StreamDeckBackgroundController" "%~dp0test_streamdeck.cpp" "%ROOT%\Controllers\StreamDeckBackgroundController\StreamDeckBackgroundController.cpp" "%ROOT%\Controllers\StreamDeckBackgroundController\StreamDeckNativeClient.cpp" /Fo"%BUILD%\\" /Fe"%BUILD%\test_streamdeck.exe"
if errorlevel 1 exit /b 1
"%BUILD%\test_streamdeck.exe"
exit /b %errorlevel%
