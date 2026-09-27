@echo off
setlocal
rem Compile only. Never runs the passive radio probe.
if not defined GOVEE_BLE_TEST_OUTDIR set "GOVEE_BLE_TEST_OUTDIR=%TEMP%\OpenRGB-Govee-BLE-tests"
if not exist "%GOVEE_BLE_TEST_OUTDIR%" mkdir "%GOVEE_BLE_TEST_OUTDIR%"
if not defined VSCMD_VER (
  for /f "usebackq tokens=*" %%v in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GOVEE_BLE_VS=%%v"
  if not defined GOVEE_BLE_VS exit /b 2
)
if not defined VSCMD_VER call "%GOVEE_BLE_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0.."
cl /nologo /EHsc /std:c++17 /O2 /W3 /utf-8 /DNOMINMAX /Itests\stubs /I. /I..\.. /I..\..\dependencies\json tests\passive_discovery_probe.cc GoveeBluetoothProtocol.cpp GoveeBluetoothSession.cpp /Fe:"%GOVEE_BLE_TEST_OUTDIR%\passive_discovery_probe.exe" /Fo:"%GOVEE_BLE_TEST_OUTDIR%\\"
set "GOVEE_BLE_RESULT=%errorlevel%"
popd
exit /b %GOVEE_BLE_RESULT%
