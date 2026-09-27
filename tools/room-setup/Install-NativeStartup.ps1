#Requires -Version 7.0
#Requires -RunAsAdministrator
param([Parameter(Mandatory=$true)][string]$ConfigDirectory,[string]$Executable,[switch]$StartNow)
# One-shot installation. No supervisor or Python process runs at logon.
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (!$Executable) { $Executable=Join-Path $repo 'dist-room\OpenRGB.exe' }
$binary=(Resolve-Path -LiteralPath $Executable).Path
$config=(Resolve-Path -LiteralPath $ConfigDirectory).Path
$result=@{startedUtc=[datetime]::UtcNow.ToString('o');executable=$binary;config=$config;completed=$false;phase='preflight';retiredBridgesRestored=$false}
$registered=$false
function Get-Sha256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path); $sha=[Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToHexString($sha.ComputeHash($stream)) }
    finally { $sha.Dispose(); $stream.Dispose() }
}
try {
    if (!(Test-Path -LiteralPath (Join-Path $config 'OpenRGB.json'))) { throw 'Prepared configuration missing.' }
    $nativeConfig=Get-Content -LiteralPath (Join-Path $config 'OpenRGB.json') -Raw | ConvertFrom-Json -AsHashtable
    if ($nativeConfig.Server.default_host -ne '127.0.0.1') { throw 'The SDK server must be configured for loopback only.' }
    $ready=Join-Path $config 'migration-ready.json'
    if (!(Test-Path -LiteralPath $ready)) { throw 'Validate the native configuration and create migration-ready.json before installing startup.' }
    $validation=Get-Content -LiteralPath $ready -Raw | ConvertFrom-Json -AsHashtable
    if ($validation.complete -ne $true -or !$validation.full_scale_sha256) { throw 'Full Scale migration is not marked complete.' }
    $fullScale=Join-Path $config 'plugins\settings\virtual-controllers\Full Scale.json'
    if (!(Test-Path -LiteralPath $fullScale) -or (Get-Sha256 $fullScale) -ne $validation.full_scale_sha256) { throw 'The validated Full Scale map has changed or is missing.' }
    $result.executableSha256=Get-Sha256 $binary
    if ($validation.executable_sha256 -and $validation.executable_sha256 -ne $result.executableSha256) { throw 'The validated native executable has changed.' }
    if ($StartNow -and (Get-Process OpenRGB,SignalRgb -ErrorAction SilentlyContinue)) { throw 'Another RGB application is running; no startup changes made.' }

    # Inspect every old entry and the destination before any startup mutation.
    $patterns=@{'SignalRGB'='SignalRgbLauncher\.exe';'SignalRGB Local Bridges'='Start-Supervisor\.ps1';'OpenRGB'='OpenRGB\.exe';'Start SignalRGB and OpenRGB Server'='SignalRGB-To-OpenRGB-Bridge[\\/]server\.exe'}
    $oldTasks=@()
    foreach($name in $patterns.Keys) {
        $task=Get-ScheduledTask -TaskName $name -TaskPath '\' -ErrorAction SilentlyContinue
        if (!$task) { continue }
        $actionText=($task.Actions | ForEach-Object {$_.Execute+' '+$_.Arguments}) -join "`n"
        if ($actionText -notmatch $patterns[$name]) { throw "Unrecognized action in existing task $name; left unchanged." }
        $oldTasks+=@{name=$name;xml=(Export-ScheduledTask -TaskName $name -TaskPath '\')}
    }
    $services=@()
    foreach($name in @('OpenRGB','SignalRgb.Service')) {
        $service=Get-CimInstance Win32_Service -Filter ("Name='"+$name+"'")
        if (!$service) { continue }
        $expected=if($name -eq 'OpenRGB'){'[\\/]OpenRGB\.exe'}else{'[\\/]SignalRgbService\.exe'}
        if ($service.PathName -notmatch $expected) { throw "Unexpected executable in service $name; left unchanged." }
        $services+=($service | Select-Object Name,StartMode,State,PathName)
    }
    $runKey='HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    $run=Get-ItemProperty -LiteralPath $runKey -ErrorAction SilentlyContinue
    $removed=@{}
    foreach($name in @('SignalRgb','BetterSignalRGBCapture','SignalRGBLocalBridges')) {
        if ($run -and $run.PSObject.Properties.Name -contains $name) { $removed[$name]=$run.$name }
    }
    # SignalRGBWallpaperBridge remains an explicit user-approved exception.
    $shortcut=Join-Path ([Environment]::GetFolderPath('Startup')) 'OpenRGB.lnk'
    $hasShortcut=Test-Path -LiteralPath $shortcut
    if ($hasShortcut) {
        $shell=New-Object -ComObject WScript.Shell
        try {
            $link=$shell.CreateShortcut($shortcut)
            if ($link.TargetPath -notmatch '[\\/]OpenRGB\.exe$') { throw 'Unexpected OpenRGB startup shortcut; left unchanged.' }
        } finally {
            if ($link) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($link) }
            [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
        }
    }
    $existing=Get-ScheduledTask -TaskName 'OpenRGB Room' -TaskPath '\' -ErrorAction SilentlyContinue
    if ($existing -and (@($existing.Actions).Count -ne 1 -or $existing.Actions[0].Execute -ne $binary)) { throw 'Existing OpenRGB Room task belongs to another executable.' }
    $user=[Security.Principal.WindowsIdentity]::GetCurrent().Name
    $arguments='--config "'+$config+'" --noautoconnect --server --server-host 127.0.0.1 --startminimized'
    $action=New-ScheduledTaskAction -Execute $binary -Argument $arguments -WorkingDirectory (Split-Path $binary)
    $trigger=New-ScheduledTaskTrigger -AtLogOn -User $user; $trigger.Delay='PT15S'
    $principal=New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
    $settings=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit ([timespan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    # StartWhenAvailable cannot race retirement: register the replacement disabled.
    $settings.Enabled=$false

    $backup=Join-Path $repo ('private\startup-backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $backup -ErrorAction Stop | Out-Null; $result.backup=$backup
    foreach($task in $oldTasks) { $task.xml | Set-Content -LiteralPath (Join-Path $backup ($task.name+'.xml')) -Encoding utf8 }
    $services | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'services.json') -Encoding utf8
    $removed | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'run-values.json') -Encoding utf8
    if ($hasShortcut) { Copy-Item -LiteralPath $shortcut -Destination (Join-Path $backup 'OpenRGB.lnk') }
    if ($existing) { Export-ScheduledTask -TaskName 'OpenRGB Room' -TaskPath '\' | Set-Content -LiteralPath (Join-Path $backup 'OpenRGB Room.xml') -Encoding utf8 }

    $result.phase='register-disabled'
    Register-ScheduledTask -TaskName 'OpenRGB Room' -TaskPath '\' -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Native OpenRGB Room devices and Full Scale. Elevated for PawnIO RAM. Wallpaper Engine is the retained bridge exception.' -Force | Out-Null
    $registered=$true
    $task=Get-ScheduledTask -TaskName 'OpenRGB Room' -TaskPath '\'
    if ($task.Settings.Enabled -or $task.Principal.RunLevel -ne 'Highest' -or $task.Actions[0].Execute -ne $binary -or $task.Actions[0].Arguments -ne $arguments) { throw 'Disabled scheduled task verification failed.' }

    $result.phase='retire-old-startup'
    foreach($task in $oldTasks) { Disable-ScheduledTask -TaskName $task.name -TaskPath '\' | Out-Null }
    foreach($service in $services) {
        if ($service.State -ne 'Stopped') { Stop-Service -Name $service.Name -ErrorAction Stop }
        Set-Service -Name $service.Name -StartupType Manual
    }
    foreach($name in $removed.Keys) { Remove-ItemProperty -LiteralPath $runKey -Name $name }
    if ($hasShortcut) { Remove-Item -LiteralPath $shortcut }

    $result.phase='enable-native-startup'
    Enable-ScheduledTask -TaskName 'OpenRGB Room' -TaskPath '\' | Out-Null
    $task=Get-ScheduledTask -TaskName 'OpenRGB Room' -TaskPath '\'
    if (!$task.Settings.Enabled) { throw 'Native scheduled task did not enable.' }
    if ($StartNow) {
        if (Get-Process OpenRGB,SignalRgb -ErrorAction SilentlyContinue) { throw 'Another RGB application appeared; native task will remain disabled.' }
        Start-ScheduledTask -TaskName 'OpenRGB Room' -TaskPath '\'
    }
    $result.completed=$true; $result.task='OpenRGB Room'; $result.phase='complete'; $result.completedUtc=[datetime]::UtcNow.ToString('o')
    Write-Output 'OpenRGB Room starts directly at logon. Wallpaper Engine bridge preserved; old RGB startup disabled.'
} catch {
    $result.error=$_.Exception.Message
    if ($registered) {
        try { Disable-ScheduledTask -TaskName 'OpenRGB Room' -TaskPath '\' -ErrorAction Stop | Out-Null; $result.nativeTaskDisabledOnFailure=$true }
        catch { $result.disableError=$_.Exception.Message; $result.nativeTaskDisabledOnFailure=$false }
    }
    # Never resurrect retired hardware bridges, even on an incomplete install.
    throw
} finally {
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $config 'startup-install-result.json') -Encoding utf8
}
