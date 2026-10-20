<#
.SYNOPSIS
    Встановлює все для розробки EdgeWidget на Windows 10/11.

.DESCRIPTION
    - Visual Studio 2022 Build Tools: компілятор MSVC, Windows SDK (з C++/WinRT), CMake
    - Python 3 (лише для aqtinstall)
    - aqtinstall і Qt (msvc2022_64) у C:\Qt
    - за бажанням: Git, Visual Studio Code

    Вже встановлене пропускається, тож скрипт можна запускати повторно.
    Потрібні winget (є в Windows 10/11) та інтернет. Інсталятор Visual Studio
    попросить права адміністратора.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\setup-dev.ps1
    powershell -ExecutionPolicy Bypass -File scripts\setup-dev.ps1 -WithGit -WithVSCode
#>
param(
    [string]$QtVersion = '6.8.3',
    [string]$QtRoot = 'C:\Qt',
    [switch]$WithGit,
    [switch]$WithVSCode
)

$ErrorActionPreference = 'Stop'
$QtArch = 'win64_msvc2022_64'
$QtDir = Join-Path $QtRoot "$QtVersion\msvc2022_64"

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Ok($text) { Write-Host "    $text" -ForegroundColor Green }

function Test-Command($name) { [bool](Get-Command $name -ErrorAction SilentlyContinue) }

function Install-Winget($id, [string[]]$extra = @()) {
    $wingetArgs = @('install', '--id', $id, '--exact', '--silent', '--accept-package-agreements', '--accept-source-agreements') + $extra
    & winget @wingetArgs
    # winget returns -1978335189 when the package is already installed and up to date.
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { throw "winget не зміг встановити $id (код $LASTEXITCODE)" }
}

# PATH змінюється інсталяторами; підтягуємо новий без перезапуску консолі.
function Update-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}

# Справжній Python, а не заглушка з Microsoft Store (вона відкриває Store замість запуску).
function Test-Python {
    if (-not (Test-Command python)) { return $false }
    $out = & python --version 2>&1
    return $LASTEXITCODE -eq 0 -and "$out" -match '^Python 3\.'
}

if (-not (Test-Command winget)) {
    throw 'Немає winget. Встанови "App Installer" з Microsoft Store і запусти скрипт знову.'
}

Step 'Visual Studio 2022 Build Tools (MSVC, Windows SDK, CMake)'
$vs = Find-VisualStudio
if ($vs) {
    Ok "вже є: $vs"
} else {
    # VCTools + рекомендовані компоненти = компілятор, Windows SDK (з C++/WinRT) і CMake.
    Install-Winget 'Microsoft.VisualStudio.2022.BuildTools' @(
        '--override', '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')
    $vs = Find-VisualStudio
    if (-not $vs) { throw 'Build Tools не знайдено після встановлення.' }
    Ok "встановлено: $vs"
}

Step 'Python 3 (для aqtinstall)'
if (Test-Python) {
    Ok "вже є: $(& python --version)"
} else {
    Install-Winget 'Python.Python.3.12'
    Update-Path
    if (-not (Test-Python)) { throw 'Python встановлено, але він не в PATH. Відкрий нову консоль і запусти скрипт ще раз.' }
    Ok "встановлено: $(& python --version)"
}

Step "Qt $QtVersion ($QtArch) у $QtRoot"
if (Test-Path (Join-Path $QtDir 'bin\Qt6Bluetooth.dll')) {
    Ok "вже є: $QtDir"
} else {
    & python -m pip install --user --quiet aqtinstall
    if ($LASTEXITCODE -ne 0) { throw 'Не вдалося встановити aqtinstall.' }
    # qtbase (Core, Gui, Widgets, Network) + qtconnectivity (Bluetooth для MediaSpin): усе під LGPL.
    # Уже встановлений Qt без Bluetooth ця ж команда просто доповнить.
    & python -m aqt install-qt windows desktop $QtVersion $QtArch -m qtconnectivity -O $QtRoot
    if ($LASTEXITCODE -ne 0) { throw 'aqtinstall не зміг встановити Qt.' }
    Ok "встановлено: $QtDir"
}

if ($WithGit) {
    Step 'Git'
    if (Test-Command git) { Ok 'вже є' } else { Install-Winget 'Git.Git'; Ok 'встановлено' }
}
if ($WithVSCode) {
    Step 'Visual Studio Code'
    if (Test-Command code) { Ok 'вже є' } else { Install-Winget 'Microsoft.VisualStudioCode'; Ok 'встановлено' }
}

Write-Host "`nГотово. Збірка:" -ForegroundColor Cyan
Write-Host "    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Run"
