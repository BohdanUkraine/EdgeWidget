<#
.SYNOPSIS
    Перемальовує resources\EdgeWidget.ico (exe та інсталер) resources\EdgeWidget.icns (macOS) і resources\edgewidget.png (Linux) з того самого вінілу, що в треї.
    Потрібно лише після зміни src\ui\VinylIcon.cpp; результат лежить у репозиторії.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\make-icon.ps1
#>
param(
    [string]$QtDir = 'C:\Qt\6.8.3\msvc2022_64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$ico = Join-Path $root 'resources\EdgeWidget.ico'
$icns = Join-Path $root 'resources\EdgeWidget.icns'
$png = Join-Path $root 'resources\edgewidget.png'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vs = & (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe') -latest -products * -property installationPath
    $cmake = Join-Path "$vs" 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}

$ErrorActionPreference = 'Continue' # попередження CMake / MSBuild у stderr
& $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtDir" 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0) { throw "cmake (налаштування) завершився з кодом $LASTEXITCODE" }
& $cmake --build $build --config Release --target make_icon 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0) { throw "збірка make_icon завершилась з кодом $LASTEXITCODE" }
$ErrorActionPreference = 'Stop'

# DLL-ки Qt беремо прямо з установки Qt.
$env:PATH = "$QtDir\bin;$env:PATH"
foreach ($out in $ico, $icns, $png) {
    & (Join-Path $build 'Release\make_icon.exe') $out
    if ($LASTEXITCODE -ne 0) { throw "make_icon завершився з кодом $LASTEXITCODE" }
}
Write-Host "`nГотово: $ico, $icns, $png" -ForegroundColor Green
