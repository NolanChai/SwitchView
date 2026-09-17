param([switch]$Offline)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
$lock = Get-Content -LiteralPath (Join-Path $repository 'dependencies.lock.json') -Raw | ConvertFrom-Json
$sources = Join-Path $repository '.cache\sources'
New-Item -ItemType Directory -Force -Path $sources | Out-Null
foreach ($dependency in $lock.dependencies) {
    $directory = Join-Path $sources $dependency.id
    if (-not (Test-Path -LiteralPath (Join-Path $directory '.git'))) {
        if ($Offline) { throw "Missing source checkout for $($dependency.id)." }
        & git -c advice.detachedHead=false clone --branch $dependency.source.tag --depth 1 --recurse-submodules --shallow-submodules $dependency.source.url $directory
        if ($LASTEXITCODE -ne 0) { throw "Could not fetch $($dependency.id) sources." }
    }
    $revision = & git -C $directory rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $revision -ne $dependency.source.commit) { throw "Unexpected source revision for $($dependency.id)." }
    $status = @(& git -C $directory status --porcelain --untracked-files=all)
    if ($LASTEXITCODE -ne 0 -or $status.Count) { throw "Source checkout has changes: $directory" }
    $actualModules = @(& git -C $directory submodule status --recursive)
    if ($LASTEXITCODE -ne 0 -or $actualModules.Count -ne @($dependency.source.submodules.PSObject.Properties).Count) { throw 'Source submodule count mismatch.' }
    foreach ($module in $dependency.source.submodules.PSObject.Properties) {
        $revision = & git -C (Join-Path $directory $module.Name) rev-parse HEAD
        if ($LASTEXITCODE -ne 0 -or $revision -ne $module.Value) { throw "Unexpected source revision: $($dependency.id)/$($module.Name)" }
    }
}
Write-Output 'Corresponding source checkouts and recursive submodules verified.'
