<#
.SYNOPSIS
    Збирає EdgeWidget і кладе готову програму в dist\ (exe + DLL-ки Qt).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1            # Release у dist\
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Run       # і запустити
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Debug -Clean
#>
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [string]$QtDir = 'C:\Qt\6.8.3\msvc2022_64',
    [switch]$Clean, # зібрати все з нуля
    [switch]$Run    # запустити після збірки
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }

# CMake з PATH або той, що йде з Visual Studio / Build Tools.
function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -property installationPath
        $bundled = Join-Path "$vs" 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $bundled) { return $bundled }
    }
    throw 'CMake не знайдено. Запусти scripts\setup-dev.ps1.'
}

# Generator for the newest Visual Studio with C++ (2022 locally, 2026 on newer CI images).
# Qt's msvc2022 build links fine with either: MSVC keeps binary compatibility.
function Find-VsGenerator {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $version = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
        switch -Wildcard ("$version") {
            '18.*' { return 'Visual Studio 18 2026' }
            '17.*' { return 'Visual Studio 17 2022' }
        }
    }
    return 'Visual Studio 17 2022'
}

function Invoke-CMake {
    # Warnings on stderr (e.g. windeployqt's dxcompiler note) must not abort the script
    # half-way through copying the DLLs: only the exit code counts.
    $ErrorActionPreference = 'Continue'
    & $cmake @args 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "cmake $($args -join ' ') завершився з кодом $LASTEXITCODE" }
}

if (-not (Test-Path (Join-Path $QtDir 'bin\Qt6Core.dll'))) {
    throw "Qt не знайдено в $QtDir. Запусти scripts\setup-dev.ps1 або передай -QtDir."
}
$cmake = Find-CMake

# Запущена програма тримає exe і DLL-ки: закриваємо її перед збіркою.
Get-Process EdgeWidget -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill(); $_.WaitForExit(5000) | Out-Null }

if ($Clean) {
    Step 'Очищення'
    Remove-Item $build, $dist -Recurse -Force -ErrorAction SilentlyContinue
}

Step 'Налаштування (CMake)'
Invoke-CMake -S $root -B $build -G (Find-VsGenerator) -A x64 "-DCMAKE_PREFIX_PATH=$QtDir"

Step "Збірка ($Config)"
Invoke-CMake --build $build --config $Config

Step "Розгортання в $dist"
Invoke-CMake --install $build --config $Config --prefix $dist

Write-Host "`nГотово: $dist\EdgeWidget.exe" -ForegroundColor Green
if ($Run) {
    Start-Process (Join-Path $dist 'EdgeWidget.exe')
}
