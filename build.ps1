param([switch]$Test, [switch]$Offline)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectDirectory = $PSScriptRoot
& (Join-Path $projectDirectory 'scripts\fetch-dependencies.ps1') -Offline:$Offline
$version = (Get-Content -LiteralPath (Join-Path $projectDirectory 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain major.minor.patch.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools and the Windows SDK.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio Desktop development with C++ is required.' }
$toolchain = Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\dshow.h') } | Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdk) { throw 'A Windows SDK with DirectShow headers is required.' }
$compiler = Join-Path $toolchain.FullName 'bin\Hostx64\x64\cl.exe'
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
$oldPath = $env:PATH
try {
    $env:PATH = (Join-Path $sdkRoot "bin\$($sdk.Name)\x64") + ';' + $oldPath
    $env:INCLUDE = @((Join-Path $toolchain.FullName 'include'), (Join-Path $sdk.FullName 'ucrt'), (Join-Path $sdk.FullName 'shared'), (Join-Path $sdk.FullName 'um'), (Join-Path $sdk.FullName 'winrt')) -join ';'
    $env:LIB = @((Join-Path $toolchain.FullName 'lib\x64'), (Join-Path $sdkRoot "Lib\$($sdk.Name)\ucrt\x64"), (Join-Path $sdkRoot "Lib\$($sdk.Name)\um\x64")) -join ';'
    $buildDirectory = Join-Path $projectDirectory 'build'
    $distDirectory = Join-Path $projectDirectory 'dist'
    New-Item -ItemType Directory -Force -Path $buildDirectory,$distDirectory | Out-Null
    $resource = (Get-Content -LiteralPath (Join-Path $projectDirectory 'src\version.rc.in') -Raw).Replace('@VERSION@', $version).Replace('@VERSION_COMMAS@', ($version.Replace('.',',') + ',0'))
    $resourcePath = Join-Path $buildDirectory 'version.rc'
    $resource | Set-Content -LiteralPath $resourcePath -Encoding ascii
    $resourceObject = Join-Path $buildDirectory 'version.res'
    $manifestPath = Join-Path $buildDirectory 'SwitchView.manifest'
    (Get-Content -LiteralPath (Join-Path $projectDirectory 'src\SwitchView.manifest') -Raw).Replace('@VERSION@', $version) | Set-Content -LiteralPath $manifestPath -Encoding utf8
    & (Join-Path $sdkRoot "bin\$($sdk.Name)\x64\rc.exe") /nologo "/fo$resourceObject" $resourcePath
    if ($LASTEXITCODE -ne 0) { throw 'Version resource build failed.' }
    $sources = @('main.cpp','video.cpp','audio.cpp','nvidia_profile.cpp') | ForEach-Object { Join-Path $projectDirectory "src\$_" }
    $arguments = @('/nologo','/std:c++17','/EHsc','/O2','/W4','/WX','/MT','/utf-8','/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0A00',"/Fo$buildDirectory\", "/Fe$distDirectory\SwitchView.exe") + $sources + @($resourceObject,'/link','/SUBSYSTEM:WINDOWS','/DYNAMICBASE','/NXCOMPAT','/MANIFEST:EMBED',"/MANIFESTINPUT:$manifestPath",'user32.lib','gdi32.lib','ole32.lib','oleaut32.lib','shell32.lib','uuid.lib','strmiids.lib','avrt.lib','propsys.lib')
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw "C++ build failed with exit code $LASTEXITCODE" }
    $lock = Get-Content -LiteralPath (Join-Path $projectDirectory 'dependencies.lock.json') -Raw | ConvertFrom-Json
    $packageFiles = @('SwitchView.exe','README.md','LICENSE','THIRD_PARTY_NOTICES.md','VERSION','install.ps1','docs/validation.md')
    foreach ($dependency in $lock.dependencies) {
        foreach ($file in $dependency.files) {
            Copy-Item -LiteralPath (Join-Path $projectDirectory "third_party\$($dependency.id)\$($file.name)") -Destination $distDirectory -Force
            $packageFiles += $file.name
        }
    }
    foreach ($file in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md','VERSION','install.ps1','docs/validation.md')) {
        $target = Join-Path $distDirectory $file
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        Copy-Item -LiteralPath (Join-Path $projectDirectory $file) -Destination $target -Force
    }
    $licenseFiles = [ordered]@{
        'mpc-video-renderer/LICENSE.txt' = 'MPC-Video-Renderer-GPL-3.0.txt'
        'lav-video/COPYING' = 'LAV-Video-GPL-2.0.txt'
        'nvapi/License.txt' = 'NVAPI-MIT.txt'
        'mpc-video-renderer/PROVENANCE.md' = 'MPC-Video-Renderer-source.md'
        'lav-video/PROVENANCE.md' = 'LAV-Video-source.md'
        'nvapi/PROVENANCE.md' = 'NVAPI-source.md'
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $distDirectory 'licenses') | Out-Null
    foreach ($file in $licenseFiles.Keys) {
        $target = 'licenses/' + $licenseFiles[$file]
        Copy-Item -LiteralPath (Join-Path $projectDirectory "third_party/$file") -Destination (Join-Path $distDirectory $target) -Force
        $packageFiles += $target
    }
    $packageFiles | Sort-Object | ForEach-Object {
        $hash = (Get-FileHash -LiteralPath (Join-Path $distDirectory $_)).Hash.ToLowerInvariant()
        "$hash  $_"
    } | Set-Content -LiteralPath (Join-Path $distDirectory 'SHA256SUMS.txt') -Encoding ascii
    if ($Test) {
        & $compiler /nologo /std:c++17 /EHsc /O2 /W4 /WX /MT /utf-8 /DUNICODE /D_UNICODE "/Fo$buildDirectory\" "/Fe$buildDirectory\audio_queue_test.exe" (Join-Path $projectDirectory 'tests\audio_queue_test.cpp') /link ole32.lib shell32.lib
        if ($LASTEXITCODE -ne 0) { throw 'Audio queue test build failed.' }
        & (Join-Path $buildDirectory 'audio_queue_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Audio queue tests failed.' }
        & (Join-Path $projectDirectory 'scripts\test-package.ps1') -Directory $distDirectory
    }
    Write-Output "Built SwitchView $version at $distDirectory\SwitchView.exe"
} finally { $env:INCLUDE = $oldInclude; $env:LIB = $oldLib; $env:PATH = $oldPath }
