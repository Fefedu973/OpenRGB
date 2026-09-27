@echo off
setlocal
if "%VCVARS%"=="" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
if errorlevel 1 exit /b 2
if not exist "%~dp0.build" mkdir "%~dp0.build"
pushd "%~dp0.build"
cl /nologo /EHsc /std:c++17 /W4 /I"%~dp0..\..\Controllers\LogitechController" /I"%~dp0..\..\dependencies\hidapi-hotplug-win\include" "%~dp0probe_receiver_readonly.cpp" /Fe:receiver-probe.exe /link "%~dp0..\..\dependencies\hidapi-hotplug-win\x64\hidapi-hotplug.lib"
if errorlevel 1 (popd & exit /b 1)
copy /y "%~dp0..\..\dependencies\hidapi-hotplug-win\x64\hidapi-hotplug.dll" . >nul
set "RESULT=%errorlevel%"
popd
exit /b %RESULT%
