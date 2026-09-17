param([switch]$Offline)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
$lock = Get-Content -LiteralPath (Join-Path $repository 'dependencies.lock.json') -Raw | ConvertFrom-Json
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($dependency in $lock.dependencies) {
    $destination = Join-Path $repository "third_party\$($dependency.id)"
    $valid = $true
    foreach ($file in $dependency.files) {
        $path = Join-Path $destination $file.name
        if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash -ne $file.sha256) { $valid = $false }
    }
    if ($valid) { continue }
    $cache = Join-Path $repository '.cache\downloads'
    New-Item -ItemType Directory -Force -Path $cache | Out-Null
    $archive = Join-Path $cache "$($dependency.id)-$($dependency.version).zip"
    if (-not (Test-Path -LiteralPath $archive)) {
        if ($Offline) { throw "Missing $($dependency.id). Run scripts\fetch-dependencies.ps1 with network access first." }
        Write-Output "Downloading $($dependency.id) $($dependency.version) from its upstream release..."
        Invoke-WebRequest -Uri $dependency.url -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive).Hash -ne $dependency.sha256) {
        throw "Checksum mismatch: $archive. No files from this archive were installed."
    }
    $stage = Join-Path $repository ('.cache\extract-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($file in $dependency.files) {
            if ([IO.Path]::GetFileName($file.name) -ne $file.name) { throw 'Runtime file names must not contain paths.' }
            $entries = @($zip.Entries | Where-Object Name -eq $file.name)
            if ($entries.Count -ne 1) { throw "Expected exactly one $($file.name) in $archive." }
            $path = Join-Path $stage $file.name
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], $path)
            if ((Get-FileHash -LiteralPath $path).Hash -ne $file.sha256) { throw "Checksum mismatch: $($file.name)" }
        }
    } finally { $zip.Dispose() }
    foreach ($file in $dependency.files) { Copy-Item -LiteralPath (Join-Path $stage $file.name) -Destination $destination -Force }
}
Write-Output 'Pinned GPU dependencies verified. No system filters were registered.'
