@echo off
setlocal
if "%VCVARS%"=="" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
if errorlevel 1 exit /b 2
if not exist "%~dp0.build" mkdir "%~dp0.build"
pushd "%~dp0.build"
cl /nologo /EHsc /std:c++17 /W4 /I"%~dp0..\..\Controllers\LogitechController" "%~dp0test_receiver_identity.cpp" /Fe:receiver-tests.exe
if errorlevel 1 (popd & exit /b 1)
receiver-tests.exe
set "RESULT=%errorlevel%"
popd
exit /b %RESULT%
