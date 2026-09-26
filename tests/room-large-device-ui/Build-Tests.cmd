@echo off
setlocal
if "%QT_ROOT%"=="" (
  echo Set QT_ROOT to a Qt 6 MSVC x64 installation.
  exit /b 2
)
if not exist "%QT_ROOT%\bin\qmake.exe" exit /b 2
if "%VCVARS%"=="" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
if errorlevel 1 exit /b 2
set "BUILD=%~dp0.build"
if not exist "%BUILD%" mkdir "%BUILD%"
pushd "%BUILD%"
"%QT_ROOT%\bin\qmake.exe" "%~dp0test_lazy_led.pro"
if errorlevel 1 (popd & exit /b 1)
nmake /nologo release
if errorlevel 1 (popd & exit /b 1)
set "PATH=%QT_ROOT%\bin;%PATH%"
set "QT_QPA_PLATFORM=offscreen"
set "QT_PLUGIN_PATH=%QT_ROOT%\plugins"
release\test_lazy_led.exe
set "RESULT=%errorlevel%"
popd
exit /b %RESULT%
