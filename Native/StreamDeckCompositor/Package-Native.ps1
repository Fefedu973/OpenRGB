# SPDX-License-Identifier: GPL-2.0-or-later
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
function Get-NativeBinaryHash([string]$Path) {
    $algorithm=[Security.Cryptography.SHA256]::Create()
    $inputStream=[IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($algorithm.ComputeHash($inputStream)).Replace('-','') }
    finally { $inputStream.Dispose(); $algorithm.Dispose() }
}
if (![IO.Path]::IsPathRooted($Destination)) { throw 'Destination must be absolute' }
$dll=Join-Path $PSScriptRoot '.build\RoomStreamDeckNative.dll'
if (!(Test-Path -LiteralPath $dll -PathType Leaf)) { throw 'Build the optional native DLL first' }
[IO.Directory]::CreateDirectory($Destination) | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $Destination 'RoomStreamDeckNative.dll')
$notices=Join-Path $Destination 'licenses\StreamDeckNative'
[IO.Directory]::CreateDirectory($notices) | Out-Null
foreach($name in @('FRIDA-LICENSE.txt','LGPL-2.0.txt','LGPL-2.1.txt','README.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $notices $name)
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\..\LICENSE') -Destination (Join-Path $notices 'OpenRGB-GPL.txt')
@{ abi=1; frida='17.18.0'; transport='in-process'; pythonRequired=$false;
   dllSha256=(Get-NativeBinaryHash $dll);
   scriptSha256=(Get-NativeBinaryHash (Join-Path $PSScriptRoot 'background-core.js'));
   devkitUrl='https://github.com/frida/frida/releases/download/17.18.0/frida-core-devkit-17.18.0-windows-x86_64.exe';
   devkitSha256='2d512af923edabb2287aed355c01214b54a8b757bb4a092a1b1a5bab6c59e886'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $notices 'build.json') -Encoding UTF8
