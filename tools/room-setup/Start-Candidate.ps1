param(
    [Parameter(Mandatory=$true)][string]$ConfigDirectory,
    [string]$Executable,
    [switch]$VirtualOnly
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (!$Executable) { $Executable=Join-Path $repo 'dist-room\OpenRGB.exe' }
$binary=(Resolve-Path -LiteralPath $Executable).Path
$config=(Resolve-Path -LiteralPath $ConfigDirectory).Path
if (!(Test-Path -LiteralPath (Join-Path $config 'OpenRGB.json'))) {
    throw 'Use a separate prepared directory containing OpenRGB.json.'
}
if (!$VirtualOnly) {
    $rgbServices=@(Get-CimInstance Win32_Service | Where-Object {
        $_.State -ne 'Stopped' -and ($_.Name -match 'OpenRGB' -or $_.PathName -match '[\\/]OpenRGB\.exe')
    })
    if ($rgbServices.Count) {
        $names=($rgbServices | ForEach-Object { "$($_.Name) ($($_.State))" }) -join ', '
        throw "Stop the existing OpenRGB service before the candidate: $names. No service was changed."
    }
    # Protected service processes may be absent from Win32_Process even though
    # Get-Process and SCM still expose their names. Closing the tray UI is not enough.
    $rgbProcesses=@(Get-Process SignalRgb,OpenRGB -ErrorAction SilentlyContinue)
    if ($rgbProcesses.Count) {
        $names=($rgbProcesses | ForEach-Object { "$($_.ProcessName) (PID $($_.Id), session $($_.SessionId))" }) -join ', '
        throw "Close RGB applications AND stop any OpenRGB service first: $names. No process was stopped."
    }
    $conflicts=@(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -match '^(SignalRgb|OpenRGB)\.exe$' -or
        ($_.Name -match '^pythonw?\.exe$' -and $_.CommandLine -match '(govee.*companion|govee_ble|ble-companion|nvidia.*bridge|gpu_bridge|fe_led_bridge|background_api)')
    })
    if ($conflicts.Count) {
        $names=($conflicts | ForEach-Object { "$($_.Name) (PID $($_.ProcessId))" }) -join ', '
        throw "Close RGB hardware owners and suspend their supervisor first: $names. No process was stopped."
    }
}
$arguments=@('--config',$config,'--noautoconnect','--gui')
if ($VirtualOnly) { $arguments+='--virtual-only' }
& $binary @arguments
if ($LASTEXITCODE) { throw "OpenRGB exited with code $LASTEXITCODE" }
