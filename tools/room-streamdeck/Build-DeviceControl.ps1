# SPDX-License-Identifier: GPL-2.0-or-later
[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'build\room-streamdeck' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run Build-DeviceControl from an x64 MSVC developer environment.'
}
if ($env:VSCMD_ARG_TGT_ARCH -and $env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    throw 'DeviceControl requires the x64 MSVC target environment.'
}
[IO.Directory]::CreateDirectory($output) | Out-Null
$source = Join-Path $PSScriptRoot 'DeviceControl.cpp'
$binary = Join-Path $output 'DeviceControl.exe'
Push-Location -LiteralPath $output
try {
    & cl.exe /nologo /EHsc /std:c++17 /MT /O2 /W4 /Brepro $source '/Fo:DeviceControl.obj' '/Fe:DeviceControl.exe' /link /SUBSYSTEM:WINDOWS /MACHINE:X64 /INCREMENTAL:NO /Brepro
    if ($LASTEXITCODE -ne 0) { throw "DeviceControl build failed: $LASTEXITCODE" }
} finally { Pop-Location }
function Get-Sha256([string]$Path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $inputFile = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($algorithm.ComputeHash($inputFile)).Replace('-','') }
    finally { $inputFile.Dispose(); $algorithm.Dispose() }
}
@{
    schema = 1
    binary = 'DeviceControl.exe'
    binarySha256 = Get-Sha256 $binary
    sourceSha256 = Get-Sha256 $source
    architecture = 'x64'
    runtime = 'MSVC static CRT (/MT)'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'DeviceControl-BUILD-INFO.json') -Encoding UTF8
Write-Output "Built $binary (not executed)."
