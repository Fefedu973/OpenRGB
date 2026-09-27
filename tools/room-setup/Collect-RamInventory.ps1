#Requires -Version 7.0
param(
    [Parameter(Mandatory=$true)][string]$ConfigDirectory,
    [string]$Executable
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (!$Executable) { $Executable=Join-Path $repo 'dist-room\OpenRGB.exe' }
$binary=(Resolve-Path -LiteralPath $Executable).Path
$config=(Resolve-Path -LiteralPath $ConfigDirectory).Path
$report=Join-Path $config 'inventory-result.json'
try {
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    $principal=New-Object Security.Principal.WindowsPrincipal($identity)
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator rights are required by the PawnIO driver for RAM detection.'
    }
    $settings=Get-Content -LiteralPath (Join-Path $config 'OpenRGB.json') -Raw | ConvertFrom-Json -AsHashtable
    $enabled=@($settings.Detectors.detectors.Keys | Where-Object {$settings.Detectors.detectors[$_] -eq $true})
    if ($enabled.Count -ne 1 -or $enabled[0] -ne 'ENE SMBus DRAM') {
        throw 'This collection requires an isolated configuration with only ENE SMBus DRAM enabled.'
    }
    foreach($section in @('GoveeBluetooth','StreamDeckBackground','NativeWallpaper')) {
        if ($settings.Contains($section) -and $settings[$section].enabled -eq $true) {
            throw "$section must be disabled in the RAM-only configuration."
        }
    }
    $owners=@(Get-Process -Name OpenRGB,SignalRgb -ErrorAction SilentlyContinue)
    if ($owners.Count) { throw 'Close other OpenRGB and SignalRGB instances before RAM detection.' }
    $profile=Join-Path $config 'profiles\ram-native-inventory.json'
    if (Test-Path -LiteralPath $profile) { throw 'Inventory already exists; use a fresh output directory.' }
    $args=@('--config',('"'+$config+'"'),'--noautoconnect','--save-profile','ram-native-inventory')
    $process=Start-Process -FilePath $binary -ArgumentList $args -WorkingDirectory (Split-Path $binary) -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(60000)) { throw 'RAM detection is still running after 60 seconds; it was not forcibly terminated.' }
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $profile)) {
        throw "RAM inventory failed (exit $($process.ExitCode)); inspect the log in $config."
    }
    $data=Get-Content -LiteralPath $profile -Raw | ConvertFrom-Json -AsHashtable
    $result=@{ok=$true;completedUtc=[datetime]::UtcNow.ToString('o');profile=$profile;controllers=@($data.controllers).Count}
    $result | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding utf8
} catch {
    @{ok=$false;completedUtc=[datetime]::UtcNow.ToString('o');error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding utf8
    throw
}
