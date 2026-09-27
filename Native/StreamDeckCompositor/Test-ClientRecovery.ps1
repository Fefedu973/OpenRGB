# SPDX-License-Identifier: GPL-2.0-or-later
# Only a synthetic DLL is loaded. No Frida, Elgato, USB or network.
[CmdletBinding()]
param([string]$VcVars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat")
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build=Join-Path $PSScriptRoot '.build\client-recovery'
[IO.Directory]::CreateDirectory($build) | Out-Null
$commands=@(
 '@echo off','setlocal',"call `"$VcVars`" >nul",'if errorlevel 1 exit /b 2',"cd /d `"$build`"",
 "cl /nologo /EHsc /std:c++17 /MD /W4 /LD `"$PSScriptRoot\test_client_fake.cpp`" /Fe:client_fixture.dll /link /INCREMENTAL:NO",
 'if errorlevel 1 exit /b 1',
 "cl /nologo /EHsc /std:c++17 /MD /W4 /DNOMINMAX /I`"$root\dependencies\json`" `"$PSScriptRoot\test_client_recovery.cpp`" `"$root\Controllers\StreamDeckBackgroundController\StreamDeckNativeClient.cpp`" /Fe:client_recovery.exe",
 'if errorlevel 1 exit /b 1','client_recovery.exe client_fixture.dll','exit /b %errorlevel%')
[IO.File]::WriteAllLines((Join-Path $build 'run.cmd'),$commands,[Text.ASCIIEncoding]::new())
& $env:ComSpec /d /c (Join-Path $build 'run.cmd')
if($LASTEXITCODE -ne 0){throw "Synthetic native-client recovery test failed: $LASTEXITCODE"}
