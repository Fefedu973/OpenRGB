@echo off
setlocal
rem Run from an x64 MSVC developer prompt. No OpenRGB process or device is opened.
where cl >nul 2>nul
if errorlevel 1 (echo An x64 MSVC developer prompt is required. & exit /b 2)
if not defined VIRTUAL_SCREEN_TEST_OUTDIR set "VIRTUAL_SCREEN_TEST_OUTDIR=%TEMP%\OpenRGB-Virtual-Screen-tests"
if not exist "%VIRTUAL_SCREEN_TEST_OUTDIR%" mkdir "%VIRTUAL_SCREEN_TEST_OUTDIR%"
pushd "%~dp0.."
cl /nologo /EHsc /std:c++17 /O2 /MD /W3 /DNOMINMAX /I..\.. /I..\..\dependencies\json tests\virtual_screen_tests.cc VirtualScreenController.cpp /Fo:"%VIRTUAL_SCREEN_TEST_OUTDIR%\\" /Fe:"%VIRTUAL_SCREEN_TEST_OUTDIR%\virtual_screen_tests.exe"
if errorlevel 1 goto failed
"%VIRTUAL_SCREEN_TEST_OUTDIR%\virtual_screen_tests.exe"
if errorlevel 1 goto failed
cl /nologo /EHsc /std:c++17 /O2 /MD /W3 /DNOMINMAX /I..\.. /I..\..\dependencies\json tests\read_surface.cc /Fo:"%VIRTUAL_SCREEN_TEST_OUTDIR%\\" /Fe:"%VIRTUAL_SCREEN_TEST_OUTDIR%\read_surface.exe"
if errorlevel 1 goto failed
cl /nologo /c /EHsc /std:c++17 /MD /W3 /DNOMINMAX /I. /I..\.. /I..\..\RGBController /I..\..\dependencies\json /I..\..\dependencies\hidapi-hotplug-win\include /I..\..\hidapi_wrapper /I..\..\i2c_smbus /I..\..\SPDAccessor /I..\..\serial_port RGBController_VirtualScreen.cpp VirtualScreenDetect.cpp /Fo:"%VIRTUAL_SCREEN_TEST_OUTDIR%\\"
if errorlevel 1 goto failed
popd
exit /b 0
:failed
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
