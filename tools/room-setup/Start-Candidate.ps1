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
    $conflicts=@(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -match '^(SignalRgb|OpenRGB)\.exe$' -or
        ($_.Name -match '^pythonw?\.exe$' -and $_.CommandLine -match '(govee.*companion|govee_ble|ble-companion|nvidia.*bridge|gpu_bridge|fe_led_bridge)')
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
