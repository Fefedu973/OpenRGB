# SPDX-License-Identifier: GPL-2.0-or-later
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$FridaDevkit,
      [string]$VcVars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
      [switch]$Tests)
$ErrorActionPreference='Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sdk = [IO.Path]::GetFullPath($FridaDevkit)
$build = Join-Path $PSScriptRoot '.build'
foreach ($file in @($VcVars,(Join-Path $sdk 'frida-core.h'),(Join-Path $sdk 'frida-core.lib'))) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required build input missing: $file" }
}
[IO.Directory]::CreateDirectory($build) | Out-Null
$js = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'background-core.js'))
if ($js.Contains(')ROOM_SCRIPT"')) { throw 'Unexpected raw-string delimiter in script' }
[IO.File]::WriteAllText((Join-Path $build 'GuardedScript.inc'), "static const char ROOM_GUARDED_SCRIPT[] = R`"ROOM_SCRIPT($js)ROOM_SCRIPT`";", [Text.UTF8Encoding]::new($false))
$commands = @(
    '@echo off', 'setlocal', "call `"$VcVars`" >nul", 'if errorlevel 1 exit /b 2',
    "cd /d `"$build`"",
    "cl /nologo /EHsc /std:c++17 /MT /W4 /D_CRT_SECURE_NO_WARNINGS /I`"$sdk`" /I`"$root\dependencies\json`" /I`"$build`" /LD `"$PSScriptRoot\Native.cpp`" /Fe:`"$build\RoomStreamDeckNative.dll`" /link /LIBPATH:`"$sdk`" /INCREMENTAL:NO",
    'if errorlevel 1 exit /b 1'
)
if ($Tests) {
    & node --test (Join-Path $PSScriptRoot 'test_background_core.cjs')
    if ($LASTEXITCODE -ne 0) { throw 'Guarded compositor JavaScript tests failed' }
    $commands += "cl /nologo /EHsc /std:c++17 /MT /W4 /I`"$root\dependencies\json`" `"$PSScriptRoot\test_rpc_response.cpp`" /Fe:`"$build\test_rpc_response.exe`""
    $commands += @('if errorlevel 1 exit /b 1', "`"$build\test_rpc_response.exe`"",'if errorlevel 1 exit /b 1')
    $commands += "cl /nologo /EHsc /std:c++17 /MT /W4 /D_CRT_SECURE_NO_WARNINGS /I`"$sdk`" /I`"$root\dependencies\json`" `"$PSScriptRoot\test_native.cpp`" /Fe:`"$build\test_native.exe`" /link /LIBPATH:`"$sdk`" /INCREMENTAL:NO"
    $commands += @('if errorlevel 1 exit /b 1', "`"$build\test_native.exe`"",'if errorlevel 1 exit /b 1')
    # Build but never run the explicitly authorized physical-device harness.
    $controller = Join-Path $root 'Controllers\StreamDeckBackgroundController'
    $commands += "cl /nologo /EHsc /std:c++17 /MD /W4 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I`"$root\dependencies\json`" /I`"$root\dependencies\httplib`" `"$PSScriptRoot\test_elgato.cpp`" `"$controller\StreamDeckNativeClient.cpp`" `"$controller\StreamDeckBackgroundController.cpp`" /Fe:`"$build\test_elgato.exe`""
    $commands += 'exit /b %errorlevel%'
}
[IO.File]::WriteAllLines((Join-Path $build 'build.cmd'),$commands,[Text.ASCIIEncoding]::new())
& $env:ComSpec /d /c (Join-Path $build 'build.cmd')
if ($LASTEXITCODE -ne 0) { throw "Native compositor build/test failed: $LASTEXITCODE" }
