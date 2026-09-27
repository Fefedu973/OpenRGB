param([Parameter(Mandatory=$true)][string]$Proposal)
$ErrorActionPreference = 'Stop'
$plan = Get-Content -LiteralPath $Proposal -Raw | ConvertFrom-Json
if (-not [IO.File]::Exists($plan.helper)) { throw 'Compiled profile selector is missing.' }
$shell = New-Object -ComObject WScript.Shell
try {
    foreach ($entry in $plan.shortcuts) {
        if ($entry.profile.Contains('"')) { throw 'Invalid profile name.' }
        $path = [IO.Path]::GetFullPath($entry.path)
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
        if ([IO.File]::Exists($path)) { throw "Refusing to overwrite existing shortcut: $path" }
        $shortcut = $shell.CreateShortcut($path)
        $shortcut.TargetPath = $plan.helper
        $shortcut.Arguments = '--profile "' + $entry.profile + '"'
        $shortcut.WorkingDirectory = [IO.Path]::GetDirectoryName($plan.helper)
        $shortcut.Description = 'Select existing OpenRGB profile: ' + $entry.profile
        $shortcut.Save()
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut)
    }
}
finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
Write-Output 'Shortcuts created. No action executed and no Elgato profile changed.'
