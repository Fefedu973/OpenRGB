param(
    [Parameter(Mandatory=$true)][string]$QtKit,
    [Parameter(Mandatory=$true)][string]$Jom,
    [string]$EffectsRoot,
    [string]$VisualMapRoot,
    [ValidateRange(1,64)][int]$Jobs=8,
    [switch]$Package,
    [switch]$PackageExisting
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$qt=(Resolve-Path -LiteralPath $QtKit).Path
$jomExe=(Resolve-Path -LiteralPath $Jom).Path
$qmake=Join-Path $qt 'bin\qmake.exe'
if (!(Test-Path -LiteralPath $qmake)) { throw 'Qt kit is missing bin/qmake.exe.' }
if (!$PackageExisting -and !(Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Run from Developer PowerShell for VS 2022 (x64).' }
if ($PackageExisting) { $Package=$true }
$oldPath=$env:PATH
$env:PATH=(Join-Path $qt 'bin')+';'+$env:PATH
function Get-BinaryHash([string]$path) {
    $hash=[System.Security.Cryptography.SHA256]::Create()
    $stream=[System.IO.File]::OpenRead($path)
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','') }
    finally { $stream.Dispose(); $hash.Dispose() }
}
function Invoke-Build([string]$root,[string]$project,[string[]]$Extra) {
    $build=Join-Path $root 'build'
    New-Item -ItemType Directory -Path $build -Force | Out-Null
    Push-Location -LiteralPath $build
    try {
        & $qmake (Join-Path $root $project) 'CONFIG+=release' 'CONFIG-=debug' 'CONFIG-=debug_and_release' 'CONFIG-=build_all' @Extra
        if ($LASTEXITCODE) { throw "qmake failed: $LASTEXITCODE" }
        & $jomExe /J $Jobs
        if ($LASTEXITCODE) { throw "jom failed: $LASTEXITCODE" }
    } finally { Pop-Location }
}
try {
    if (!$PackageExisting) { Invoke-Build $repo 'OpenRGB.pro' @() }
    if ($EffectsRoot) {
        $effects=(Resolve-Path -LiteralPath $EffectsRoot).Path
        if (!(Test-Path (Join-Path $effects 'Dependencies\QCodeEditor\include\internal\QCodeEditor.hpp'))) {
            throw 'Initialize Effects QCodeEditor and SimplexNoise submodules first.'
        }
        if (!$PackageExisting) { Invoke-Build $effects 'OpenRGBEffectsPlugin.pro' @("OPENRGB_ROOM_ROOT=$($repo.Replace('\','/'))") }
    }
    if ($VisualMapRoot) {
        $visualMap=(Resolve-Path -LiteralPath $VisualMapRoot).Path
        if (!$PackageExisting) {
            if (!(Get-Command sed.exe -ErrorAction SilentlyContinue)) {
                $gitCommand=Get-Command git.exe -ErrorAction Stop
                $gitTools=Join-Path (Split-Path (Split-Path $gitCommand.Source)) 'usr\bin'
                if (!(Test-Path -LiteralPath (Join-Path $gitTools 'sed.exe'))) { throw 'Visual Map requires sed (included with Git for Windows).' }
                $env:PATH+=';'+$gitTools
            }
            Invoke-Build $visualMap 'OpenRGBVisualMapPlugin.pro' @("OPENRGB_ROOM_ROOT=$($repo.Replace('\','/'))",'QMAKE_STREAM_EDITOR=sed')
        }
    }
    if ($Package) {
        $dist=Join-Path $repo 'dist-room'
        New-Item -ItemType Directory -Path $dist -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $repo 'build\release\OpenRGB.exe') -Destination $dist
        # Non-Qt runtime dependencies are supplied by the OpenRGB build itself.
        Get-ChildItem -LiteralPath (Join-Path $repo 'build\release') -File | Where-Object {
            $_.Extension -in @('.dll','.bin')
        } | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $dist }
        $deploy=Join-Path $qt 'bin\windeployqt.exe'
        & $deploy --release --no-translations --compiler-runtime (Join-Path $dist 'OpenRGB.exe')
        if ($LASTEXITCODE) { throw 'windeployqt failed for OpenRGB.' }
        if ($EffectsRoot) {
            $dll=Join-Path $effects 'build\release\OpenRGBEffectsPlugin.dll'
            if (!(Test-Path -LiteralPath $dll)) { throw 'Effects DLL not found at expected build path.' }
            $pluginDir=Join-Path $dist 'plugins'
            New-Item -ItemType Directory -Path $pluginDir -Force | Out-Null
            Copy-Item -LiteralPath $dll -Destination $pluginDir
            & $deploy --release --no-translations --compiler-runtime --dir $dist $dll
            if ($LASTEXITCODE) { throw 'windeployqt failed for Effects.' }
        }
        if ($VisualMapRoot) {
            $dll=Join-Path $visualMap 'build\release\OpenRGBVisualMapPlugin.dll'
            if ($PackageExisting -and !(Test-Path -LiteralPath $dll)) {
                $dll=Join-Path $visualMap '.build\release\OpenRGBVisualMapPlugin.dll'
            }
            if (!(Test-Path -LiteralPath $dll)) { throw 'Visual Map DLL not found at expected build path.' }
            $pluginDir=Join-Path $dist 'plugins'
            New-Item -ItemType Directory -Path $pluginDir -Force | Out-Null
            Copy-Item -LiteralPath $dll -Destination $pluginDir
            & $deploy --release --no-translations --compiler-runtime --dir $dist $dll
            if ($LASTEXITCODE) { throw 'windeployqt failed for Visual Map.' }
        }
        Copy-Item -LiteralPath (Join-Path $repo 'ROOM.md') -Destination $dist
        $manifest=@{coreCommit=(& git -C $repo rev-parse HEAD);sdkImageSchema=1; sdkVersion=7;packagedAtUtc=[DateTime]::UtcNow.ToString('o');existingBuild=[bool]$PackageExisting}
        if ($EffectsRoot) { $manifest.effectsCommit=(& git -C $effects rev-parse HEAD) }
        if ($VisualMapRoot) { $manifest.visualMapCommit=(& git -C $visualMap rev-parse HEAD) }
        $manifest.binarySha256=Get-BinaryHash (Join-Path $dist 'OpenRGB.exe')
        if ($EffectsRoot) { $manifest.effectsBinarySha256=Get-BinaryHash (Join-Path $dist 'plugins\OpenRGBEffectsPlugin.dll') }
        if ($VisualMapRoot) { $manifest.visualMapBinarySha256=Get-BinaryHash (Join-Path $dist 'plugins\OpenRGBVisualMapPlugin.dll') }
        $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dist 'BUILD-INFO.json') -Encoding utf8
        Write-Output "Portable local build: $dist (not started)."
    }
} finally { $env:PATH=$oldPath }
