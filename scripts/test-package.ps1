param([Parameter(Mandatory)][string]$Directory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$directoryPath = (Resolve-Path -LiteralPath $Directory).Path
$repository = Split-Path $PSScriptRoot -Parent
$version = (Get-Content -LiteralPath (Join-Path $repository 'VERSION') -Raw).Trim()
$exe = Join-Path $directoryPath 'SwitchView.exe'
if ((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $version) { throw 'Executable version does not match VERSION.' }
$bytes = [IO.File]::ReadAllBytes($exe)
$peOffset = [BitConverter]::ToInt32($bytes, 60)
if ([BitConverter]::ToUInt16($bytes, $peOffset + 4) -ne 0x8664) { throw 'Release executable must be x64.' }
$testRoot = Join-Path $repository ('.cache\package-test-' + [guid]::NewGuid().ToString('N'))
$target = Join-Path $testRoot 'installed'
& (Join-Path $directoryPath 'install.ps1') -InstallDirectory $target -NoShortcut
if ((Get-FileHash -LiteralPath (Join-Path $target 'SwitchView.exe')).Hash -ne (Get-FileHash -LiteralPath $exe).Hash) { throw 'Installed executable differs from package.' }
$runtimeReport = Join-Path $testRoot 'runtime'
$runtimeArgs = '--check-runtime --output "' + $runtimeReport + '"'
$process = Start-Process -FilePath (Join-Path $target 'SwitchView.exe') -ArgumentList $runtimeArgs -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Runtime check timed out.' }
if ($process.ExitCode -ne 0) { throw "GPU runtime validation failed: $(Get-Content -LiteralPath (Join-Path $runtimeReport 'runtime-check.txt') -Raw)" }
# A missing optional renderer must produce a useful error, not load a system filter.
$missing = Join-Path $testRoot 'missing dependency'
New-Item -ItemType Directory -Path $missing -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination $missing
$missingArgs = '--check-runtime --output "' + (Join-Path $testRoot 'missing-report') + '"'
$process = Start-Process -FilePath (Join-Path $missing 'SwitchView.exe') -ArgumentList $missingArgs -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'Missing-dependency check timed out.' }
$failureReport = Get-Content -LiteralPath (Join-Path $testRoot 'missing-report\runtime-check.txt') -Raw
if ($process.ExitCode -ne 1 -or $failureReport -notlike '*renderer is missing*') { throw 'Missing renderer did not fail explicitly.' }
# Test the release installer from its portable layout, including paths with spaces.
$corrupt = Join-Path $testRoot 'corrupt package'
New-Item -ItemType Directory -Path $corrupt -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $directoryPath 'install.ps1') -Destination $corrupt
Set-Content -LiteralPath (Join-Path $corrupt 'SwitchView.exe') -Value 'deliberately damaged package'
Set-Content -LiteralPath (Join-Path $corrupt 'SHA256SUMS.txt') -Value (('0' * 64) + '  SwitchView.exe')
$rejected = $false
try { & (Join-Path $corrupt 'install.ps1') -InstallDirectory (Join-Path $testRoot 'must-not-install') -NoShortcut }
catch { if ($_.Exception.Message -notlike '*checksum mismatch*') { throw }; $rejected = $true }
if (-not $rejected -or (Test-Path -LiteralPath (Join-Path $testRoot 'must-not-install'))) { throw 'Corrupt package was not rejected before installation.' }
Set-Content -LiteralPath (Join-Path $corrupt 'SHA256SUMS.txt') -Value (('0' * 64) + '  ../escape.exe')
$rejected = $false
try { & (Join-Path $corrupt 'install.ps1') -InstallDirectory (Join-Path $testRoot 'must-not-install') -NoShortcut }
catch { if ($_.Exception.Message -notlike '*Unsafe package path*') { throw }; $rejected = $true }
if (-not $rejected) { throw 'Unsafe package path was not rejected.' }
Write-Output 'Package checks passed: x64/version, portable install, runtime DLLs/ABI, missing dependency, corruption and path rejection.'
