$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$binary=Join-Path $repo 'dist-room\OpenRGB.exe'
$task=Get-ScheduledTask -TaskName 'OpenRGB Room' -ErrorAction SilentlyContinue
if (!$task -or !$task.Settings.Enabled) {
    throw 'OpenRGB Room startup is not installed or enabled. Run Install-NativeStartup.ps1 with the validated configuration first.'
}
if (@($task.Actions).Count -ne 1 -or $task.Actions[0].Execute -ne $binary) {
    throw 'The OpenRGB Room task points to another installation.'
}
if ($task.State -eq 'Running') {
    Write-Output 'OpenRGB Room is already running; open it from its tray icon.'
} else {
    Start-ScheduledTask -TaskName 'OpenRGB Room'
    Write-Output 'OpenRGB Room started. Its window is available from the tray icon.'
}
