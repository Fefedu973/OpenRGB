@echo off
setlocal
rem Compile-only check of the native controller. Does not link/run OpenRGB.
if not defined GOVEE_BLE_TEST_OUTDIR set "GOVEE_BLE_TEST_OUTDIR=%TEMP%\OpenRGB-Govee-BLE-tests"
if not exist "%GOVEE_BLE_TEST_OUTDIR%" mkdir "%GOVEE_BLE_TEST_OUTDIR%"
if not defined VSCMD_VER (
  for /f "usebackq tokens=*" %%v in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GOVEE_BLE_VS=%%v"
  if not defined GOVEE_BLE_VS exit /b 2
)
if not defined VSCMD_VER call "%GOVEE_BLE_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0.."
cl /nologo /c /EHsc /std:c++17 /W3 /DWIN32 /D_WINDOWS /DNOMINMAX /I. /I..\.. /I..\..\RGBController /I..\..\dependencies\json /I..\..\dependencies\hidapi-hotplug-win\include /I..\..\hidapi_wrapper /I..\..\i2c_smbus /I..\..\SPDAccessor /I..\..\serial_port GoveeBluetoothController_Windows.cpp GoveeBluetoothDetect_Windows.cpp RGBController_GoveeBluetooth_Windows.cpp /Fo:"%GOVEE_BLE_TEST_OUTDIR%\\"
set "GOVEE_BLE_RESULT=%errorlevel%"
popd
exit /b %GOVEE_BLE_RESULT%
