param(
    [Parameter(Mandatory=$true)][string]$QtKit,
    [Parameter(Mandatory=$true)][string]$Jom,
    [string]$EffectsRoot,
    [string]$VisualMapRoot,
    [string]$WebView2Sdk,
    [switch]$NativeStreamDeck,
    [string]$FridaDevkit,
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
$withNativeStreamDeck=[bool]$NativeStreamDeck -or ![string]::IsNullOrWhiteSpace($FridaDevkit)
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
    if ($withNativeStreamDeck -and !$PackageExisting) {
        if ([string]::IsNullOrWhiteSpace($FridaDevkit)) { throw '-NativeStreamDeck requires -FridaDevkit unless -PackageExisting is used.' }
        & (Join-Path $repo 'Native\StreamDeckCompositor\Build-Native.ps1') -FridaDevkit $FridaDevkit
        if ($LASTEXITCODE) { throw 'Optional native Stream Deck compositor build failed.' }
    }
    if (!$PackageExisting) { Invoke-Build $repo 'OpenRGB.pro' @() }
    if ($EffectsRoot) {
        $effects=(Resolve-Path -LiteralPath $EffectsRoot).Path
        if (!$WebView2Sdk) { $WebView2Sdk=Join-Path $effects 'build\webview2-sdk' }
        if (!(Test-Path (Join-Path $effects 'Dependencies\QCodeEditor\include\internal\QCodeEditor.hpp'))) {
            throw 'Initialize Effects QCodeEditor and SimplexNoise submodules first.'
        }
        if (!$PackageExisting) { Invoke-Build $effects 'OpenRGBEffectsPlugin.pro' @("OPENRGB_ROOM_ROOT=$($repo.Replace('\','/'))", "WEBVIEW2_SDK=$($WebView2Sdk.Replace('\','/'))") }
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
        # This one-shot SDK client is independent of the core build. Compile it
        # explicitly: neither qmake nor windeployqt discovers this helper.
        & (Join-Path $repo 'tools\room-streamdeck\Build-ProfileSelect.ps1')
        $profileHelperBuild=Join-Path $repo 'build\room-streamdeck'
        Copy-Item -LiteralPath (Join-Path $profileHelperBuild 'ProfileSelect.exe') -Destination $dist
        Copy-Item -LiteralPath (Join-Path $profileHelperBuild 'ProfileSelect-BUILD-INFO.json') -Destination $dist
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
            # WebView2 is loaded dynamically, so windeployqt cannot discover it.
            $browserLoader=Join-Path $WebView2Sdk 'build\native\x64\WebView2Loader.dll'
            $webView2Included=Test-Path -LiteralPath $browserLoader
            if ($webView2Included) {
                $browserLicense=Join-Path $WebView2Sdk 'LICENSE.txt'
                if (!(Test-Path -LiteralPath $browserLicense)) { throw 'WebView2 SDK license is required for redistribution.' }
                Copy-Item -LiteralPath $browserLoader -Destination $pluginDir
                Copy-Item -LiteralPath $browserLicense -Destination (Join-Path $pluginDir 'WebView2-LICENSE.txt')
                if (Test-Path -LiteralPath (Join-Path $effects 'Documentation\WEBPAGE.md')) {
                    Copy-Item -LiteralPath (Join-Path $effects 'Documentation\WEBPAGE.md') -Destination $dist
                }
            }
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
        Copy-Item -LiteralPath (Join-Path $repo 'docs\room\DEMARRAGE.md') -Destination $dist
        if ($withNativeStreamDeck) {
            & (Join-Path $repo 'Native\StreamDeckCompositor\Package-Native.ps1') -Destination $dist
        }
        $manifest=@{coreCommit=(& git -C $repo rev-parse HEAD);sdkImageSchema=1; sdkVersion=7;packagedAtUtc=[DateTime]::UtcNow.ToString('o');existingBuild=[bool]$PackageExisting}
        $manifest.coreWorkingTreeDirty=[bool](& git -C $repo status --porcelain --untracked-files=normal)
        $manifest.profileSelectBinarySha256=Get-BinaryHash (Join-Path $dist 'ProfileSelect.exe')
        $manifest.profileSelectSourceSha256=Get-BinaryHash (Join-Path $repo 'tools\room-streamdeck\ProfileSelect.cpp')
        $manifest.nativeStreamDeckIncluded=$withNativeStreamDeck
        if ($withNativeStreamDeck) {
            $manifest.nativeStreamDeckBinarySha256=Get-BinaryHash (Join-Path $dist 'RoomStreamDeckNative.dll')
            $manifest.fridaCoreVersion='17.18.0'
            $manifest.nativeStreamDeckScriptSha256=Get-BinaryHash (Join-Path $repo 'Native\StreamDeckCompositor\background-core.js')
        }
        if ($EffectsRoot) {
            $manifest.effectsCommit=(& git -C $effects rev-parse HEAD)
            $manifest.effectsWorkingTreeDirty=[bool](& git -C $effects status --porcelain --untracked-files=normal)
            $manifest.webView2LoaderIncluded=[bool]$webView2Included
            if ($webView2Included) {
                $manifest.webView2LoaderSha256=Get-BinaryHash (Join-Path $dist 'plugins\WebView2Loader.dll')
                $manifest.webView2LoaderVersion=(Get-Item -LiteralPath $browserLoader).VersionInfo.FileVersion
                $manifest.webView2Runtime='Uses installed Microsoft Edge WebView2 Evergreen runtime; not bundled'
            }
        }
        if ($VisualMapRoot) {
            $manifest.visualMapCommit=(& git -C $visualMap rev-parse HEAD)
            $manifest.visualMapWorkingTreeDirty=[bool](& git -C $visualMap status --porcelain --untracked-files=normal)
        }
        $manifest.binarySha256=Get-BinaryHash (Join-Path $dist 'OpenRGB.exe')
        if ($EffectsRoot) { $manifest.effectsBinarySha256=Get-BinaryHash (Join-Path $dist 'plugins\OpenRGBEffectsPlugin.dll') }
        if ($VisualMapRoot) { $manifest.visualMapBinarySha256=Get-BinaryHash (Join-Path $dist 'plugins\OpenRGBVisualMapPlugin.dll') }
        $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dist 'BUILD-INFO.json') -Encoding utf8
        Write-Output "Portable local build: $dist (not started)."
    }
} finally { $env:PATH=$oldPath }
