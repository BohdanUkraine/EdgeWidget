<#
.SYNOPSIS
    Збирає інсталер EdgeWidget: out\EdgeWidget-Setup-<версія>.exe (Inno Setup).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\package.ps1
#>
param(
    [string]$QtDir = 'C:\Qt\6.8.3\msvc2022_64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$out = Join-Path $root 'out'
$stage = Join-Path $out 'stage'

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }

function Find-Iscc {
    $cmd = Get-Command iscc -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($dir in "$env:LOCALAPPDATA\Programs\Inno Setup 6", "${env:ProgramFiles(x86)}\Inno Setup 6", "$env:ProgramFiles\Inno Setup 6") {
        $exe = Join-Path $dir 'ISCC.exe'
        if (Test-Path $exe) { return $exe }
    }
    throw 'Inno Setup не знайдено. Встанови: winget install --id JRSoftware.InnoSetup -e --scope user'
}

$iscc = Find-Iscc
$version = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(EdgeWidget VERSION ([0-9.]+)').Groups[1].Value
if (-not $version) { throw 'Не знайшов версію в CMakeLists.txt (project(EdgeWidget VERSION x.y.z))' }

# Збірка (і dist\ для себе) — звичайним скриптом.
& (Join-Path $PSScriptRoot 'build.ps1') -QtDir $QtDir

# Інсталер пакує чисту копію: у dist\ можуть лежати DLL-ки старих збірок.
Step "Розгортання в $stage"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vs = & (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe') -latest -products * -property installationPath
    $cmake = Join-Path "$vs" 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$ErrorActionPreference = 'Continue' # windeployqt пише попередження в stderr
& $cmake --install $build --config Release --prefix $stage 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0) { throw "cmake --install завершився з кодом $LASTEXITCODE" }

Step "Інсталер $version"
& $iscc "/DAppVersion=$version" "/DStageDir=$stage" (Join-Path $root 'installer\EdgeWidget.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC завершився з кодом $LASTEXITCODE" }
$ErrorActionPreference = 'Stop'

$setup = Join-Path $out "EdgeWidget-Setup-$version.exe"
Write-Host "`nГотово: $setup ($([math]::Round((Get-Item $setup).Length / 1MB, 1)) МБ)" -ForegroundColor Green
