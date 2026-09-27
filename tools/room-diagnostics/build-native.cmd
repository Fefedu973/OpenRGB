@echo off
setlocal
if not defined VCVARS set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call "%VCVARS%" x64 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0\..\.."
if not exist tools\room-diagnostics\.build mkdir tools\room-diagnostics\.build
cl /nologo /EHsc /std:c++17 /O2 /MD /utf-8 /DNOMINMAX /Itools\room-diagnostics\stubs /I. /Idependencies\json /Idependencies\hidapi-hotplug-win\include tools\room-diagnostics\native_devices.cc Controllers\GoveeBluetoothController\GoveeBluetoothProtocol.cpp Controllers\GoveeBluetoothController\GoveeBluetoothSession.cpp Controllers\KBHEController\KBHEController.cpp Controllers\AlienwareMonitorController\AlienwareMonitorController\AlienwareMonitorController.cpp Controllers\AlienwareMonitorController\AlienwareMonitorController\AlienwareMonitorProfiles.cpp StringUtils.cpp /Fo:tools\room-diagnostics\.build\ /Fe:tools\room-diagnostics\.build\native_devices.exe /link dependencies\hidapi-hotplug-win\x64\hidapi-hotplug.lib
if errorlevel 1 exit /b %errorlevel%
copy /y dependencies\hidapi-hotplug-win\x64\hidapi-hotplug.dll tools\room-diagnostics\.build\ >nul
exit /b %errorlevel%
