param([switch]$SkipBuild, [switch]$Offline, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
if (-not $SkipBuild) { & (Join-Path $repository 'build.ps1') -Test -Offline:$Offline }
else { & (Join-Path $PSScriptRoot 'test-package.ps1') -Directory (Join-Path $repository 'dist') }
$dirty = @(& git -C $repository status --porcelain)
if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw 'Commit the reviewed source before packaging a release.' }
$version = (Get-Content -LiteralPath (Join-Path $repository 'VERSION') -Raw).Trim()
$commit = & git -C $repository rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve release source commit.' }
& (Join-Path $PSScriptRoot 'fetch-sources.ps1') -Offline:$Offline
$lock = Get-Content -LiteralPath (Join-Path $repository 'dependencies.lock.json') -Raw | ConvertFrom-Json
$output = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $repository 'release' }
$stage = Join-Path $repository ('.cache\release-' + [guid]::NewGuid().ToString('N'))
$portable = Join-Path $stage "SwitchView-$version-windows-x64"
$sourceBundle = Join-Path $stage 'dependencies'
New-Item -ItemType Directory -Force -Path $output,$portable,$sourceBundle | Out-Null
$checksums = Get-Content -LiteralPath (Join-Path $repository 'dist\SHA256SUMS.txt')
$files = @($checksums | ForEach-Object { ($_ -split '  ', 2)[1] }) + @('SHA256SUMS.txt')
foreach ($file in $files) {
    $target = Join-Path $portable $file
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repository "dist/$file") -Destination $target
}
foreach ($dependency in $lock.dependencies) {
    $checkout = Join-Path $repository ".cache\sources\$($dependency.id)"
    $destination = Join-Path $sourceBundle $dependency.id
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $trees = @([pscustomobject]@{Path='';Commit=$dependency.source.commit})
    $trees += @($dependency.source.submodules.PSObject.Properties | ForEach-Object { [pscustomobject]@{Path=$_.Name;Commit=$_.Value} })
    foreach ($tree in $trees) {
        $tarPath = Join-Path $stage ([guid]::NewGuid().ToString('N') + '.tar')
        $prefix = if ($tree.Path) { $tree.Path + '/' } else { '' }
        & git -C (Join-Path $checkout $tree.Path) archive --format=tar "--prefix=$prefix" "--output=$tarPath" $tree.Commit
        if ($LASTEXITCODE -ne 0) { throw 'Source export failed.' }
        & tar -xf $tarPath -C $destination
        if ($LASTEXITCODE -ne 0) { throw 'Source extraction failed.' }
    }
}
Copy-Item -LiteralPath (Join-Path $repository 'dependencies.lock.json') -Destination $sourceBundle
Copy-Item -LiteralPath (Join-Path $repository 'docs\dependency-sources.md') -Destination (Join-Path $sourceBundle 'README.md')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$portableZip = Join-Path $output "SwitchView-$version-windows-x64.zip"
$sourceZip = Join-Path $output "SwitchView-$version-source.zip"
$dependenciesZip = Join-Path $output "SwitchView-$version-dependency-sources.zip"
foreach ($asset in @($portableZip,$sourceZip,$dependenciesZip)) { if (Test-Path -LiteralPath $asset) { throw "Release asset already exists: $asset. Choose a new version or output directory." } }
[IO.Compression.ZipFile]::CreateFromDirectory($portable, $portableZip)
& git -C $repository archive --format=zip "--output=$sourceZip" HEAD
if ($LASTEXITCODE -ne 0) { throw 'SwitchView source export failed.' }
[IO.Compression.ZipFile]::CreateFromDirectory($sourceBundle, $dependenciesZip)
@($portableZip,$sourceZip,$dependenciesZip) | ForEach-Object {
    "$((Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($_))"
} | Set-Content -LiteralPath (Join-Path $output "SwitchView-$version-SHA256SUMS.txt") -Encoding ascii
Write-Output "Release $version prepared from $commit in $output. Upload all four files together."
