param([switch]$DesktopShortcut, [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Programs\SwitchView'), [switch]$NoShortcut)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceDirectory = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'SwitchView.exe')) { $PSScriptRoot } else { Join-Path $PSScriptRoot 'dist' }
$manifest = Join-Path $sourceDirectory 'SHA256SUMS.txt'
if (-not (Test-Path -LiteralPath $manifest)) { throw 'Download and extract a release, or run build.ps1 first.' }
$files = @(Get-Content -LiteralPath $manifest | ForEach-Object {
    if ($_ -notmatch '^([0-9a-f]{64})  ([A-Za-z0-9_.\-/]+)$') { throw 'Invalid package checksum entry.' }
    $expected = $Matches[1]; $name = $Matches[2]
    if ([IO.Path]::IsPathRooted($name) -or $name.Split('/') -contains '..' -or $name.Contains('\')) { throw 'Unsafe package path.' }
    $file = Join-Path $sourceDirectory $name
    if ((Get-FileHash -LiteralPath $file).Hash -ne $expected) { throw "Package checksum mismatch: $name" }
    $name
})
if ($files -notcontains 'SwitchView.exe') { throw 'Package executable is missing from the checksum manifest.' }
$installedExecutable = [IO.Path]::GetFullPath((Join-Path $InstallDirectory 'SwitchView.exe'))
if (Get-Process SwitchView -ErrorAction SilentlyContinue | Where-Object Path -eq $installedExecutable) { throw 'Exit the installed SwitchView before updating it.' }
foreach ($file in @($files) + @('SHA256SUMS.txt')) {
    $target = Join-Path $InstallDirectory $file
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceDirectory $file) -Destination $target -Force
}
if (-not $NoShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcutFolders = @([Environment]::GetFolderPath('Programs'))
    if ($DesktopShortcut) { $shortcutFolders += [Environment]::GetFolderPath('Desktop') }
    foreach ($folder in $shortcutFolders) {
        $shortcut = $shell.CreateShortcut((Join-Path $folder 'SwitchView.lnk'))
        $shortcut.TargetPath = $installedExecutable
        $shortcut.WorkingDirectory = [IO.Path]::GetDirectoryName($installedExecutable)
        $shortcut.Description = 'Fullscreen capture-card video with direct audio monitoring'
        $shortcut.Save()
    }
}
Write-Output "Installed $installedExecutable"
