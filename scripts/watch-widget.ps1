<#
.SYNOPSIS
    Стежить за EdgeWidget і записує, чому він закрився: код виходу, що тоді працювало
    (збірка, інсталятор, taskkill), події Windows, хвіст логу самого віджета.

.DESCRIPTION
    Працює у фоні, поки його не зупинити. Журнал: out\watch\watch.log, логи віджета: out\watch\widget-*.log.
    Уже запущений віджет лише підхоплює (його лог недоступний). Після першого закриття запускає
    свій екземпляр з логом і перезапускає його після кожного закриття, крім «Вийти» в треї.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\watch-widget.ps1          # стежити (у цьому вікні)
    powershell -ExecutionPolicy Bypass -File scripts\watch-widget.ps1 -Stop    # зупинити фоновий
#>
param(
    [switch]$Stop
)

$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'dist\EdgeWidget.exe'
$dir = Join-Path $root 'out\watch'
$log = Join-Path $dir 'watch.log'

if ($Stop) {
    Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe'" |
        Where-Object { $_.CommandLine -match 'watch-widget\.ps1' -and $_.CommandLine -notmatch '-Stop' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force; "Зупинено спостерігач (pid $($_.ProcessId))" }
    return
}

# One watcher at a time.
$mutex = New-Object System.Threading.Mutex($false, 'Local\EdgeWidgetWatcher')
if (-not $mutex.WaitOne(0)) { Write-Host 'Спостерігач уже працює.'; return }

New-Item -ItemType Directory -Force $dir | Out-Null

function Log([string]$text) {
    $line = '{0:yyyy-MM-dd HH:mm:ss.fff}  {1}' -f (Get-Date), $text
    Add-Content -Path $log -Value $line -Encoding utf8
    Write-Host $line
}

function Describe-Exit([long]$code) {
    $hex = '0x{0:X8}' -f ($code -band 0xFFFFFFFFL)
    $why = switch ($hex) {
        '0x00000000' { 'звичайний вихід програми (Вийти в треї, вихід із Windows, або програма завершилась сама)' }
        '0x00000001' { 'вбито ззовні (taskkill /F чи подібне)' }
        '0xFFFFFFFF' { 'вбито ззовні (Process.Kill / Stop-Process — так робить scripts\build.ps1)' }
        '0xC0000005' { 'ПАДІННЯ: звернення до недоступної пам''яті (access violation)' }
        '0xC0000409' { 'ПАДІННЯ: аварійне завершення (qFatal / abort / fail-fast)' }
        '0xC00000FD' { 'ПАДІННЯ: переповнення стеку' }
        '0xC0000374' { 'ПАДІННЯ: пошкоджена купа (heap corruption)' }
        '0xC0000135' { 'не стартував: бракує DLL' }
        '0xC0000139' { 'не стартував: у DLL немає потрібної функції (не та / пошкоджена DLL)' }
        '0x40010004' { 'завершено системою (вихід із сеансу / зупинка налагоджувачем)' }
        default      { 'невідомий код' }
    }
    "$hex — $why"
}

# Processes that can close the widget, seen in the last minute: pid -> info.
$suspects = @{}
# Not MSBuild: its worker nodes idle around for minutes after every build (cmake shows a build anyway).
$suspectFilter = "Name='cmake.exe' OR Name='windeployqt.exe' OR Name='ISCC.exe' " +
                 "OR Name='taskkill.exe' OR Name LIKE 'unins%' OR Name LIKE 'EdgeWidget-Setup%' " +
                 "OR Name='powershell.exe' OR Name='pwsh.exe'"
function Sample-Suspects {
    $now = Get-Date
    foreach ($p in Get-CimInstance Win32_Process -Filter $suspectFilter -ErrorAction SilentlyContinue) {
        if ($p.ProcessId -eq $PID) { continue }
        $cmd = [string]$p.CommandLine
        if ($p.Name -match '^(powershell|pwsh)\.exe$' -and $cmd -notmatch 'build\.ps1|package\.ps1|make-icon\.ps1|EdgeWidget|Stop-Process') { continue }
        $key = [int]$p.ProcessId
        if ($suspects.ContainsKey($key)) { $suspects[$key].Last = $now }
        else { $suspects[$key] = [pscustomobject]@{ First = $now; Last = $now; Name = $p.Name; Cmd = $cmd } }
    }
    foreach ($key in @($suspects.Keys)) { if (($now - $suspects[$key].Last).TotalSeconds -gt 60) { $suspects.Remove($key) } }
}

function Report-Exit($proc, [string]$widgetLog, [datetime]$started) {
    $proc.WaitForExit()
    $code = $proc.ExitCode
    $uptime = (Get-Date) - $started
    Log ('ЗАКРИВСЯ pid {0} після {1:d\.hh\:mm\:ss} роботи: {2}' -f $proc.Id, $uptime, (Describe-Exit $code))

    Sample-Suspects
    $recent = $suspects.Values | Where-Object { ((Get-Date) - $_.Last).TotalSeconds -le 30 }
    if ($recent) {
        Log '  працювало в цей час:'
        foreach ($s in $recent) { Log ('    {0:HH:mm:ss} {1}  {2}' -f $s.First, $s.Name, ($s.Cmd -replace '\s+', ' ').Substring(0, [Math]::Min(200, ($s.Cmd -replace '\s+', ' ').Length))) }
    }

    $since = (Get-Date).AddMinutes(-2)
    $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = $since } -ErrorAction SilentlyContinue |
                    Where-Object { $_.Message -match 'EdgeWidget' })
    $events += @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; StartTime = $since; Id = 1074, 6006, 6008, 41 } -ErrorAction SilentlyContinue)
    foreach ($e in $events) {
        Log ('  подія {0:HH:mm:ss} {1} #{2}: {3}' -f $e.TimeCreated, $e.ProviderName, $e.Id, (($e.Message -split "`r?`n" | Select-Object -First 3) -join ' | '))
    }

    $deliberate = $false
    if ($widgetLog -and (Test-Path $widgetLog)) {
        $tail = Get-Content $widgetLog -Encoding utf8 -Tail 25
        $deliberate = [bool]($tail | Select-String -SimpleMatch '[app] quit from the tray menu')
        Log "  кінець логу віджета ($widgetLog):"
        foreach ($line in $tail) { Log "    $line" }
    } else {
        Log '  лог віджета недоступний (цей екземпляр запускав не спостерігач)'
    }
    return [pscustomobject]@{ Code = $code; Deliberate = $deliberate; Own = [bool]$widgetLog }
}

function Start-Widget {
    $widgetLog = Join-Path $dir ('widget-{0:yyyyMMdd-HHmmss}.log' -f (Get-Date))
    $env:QT_FORCE_STDERR_LOGGING = '1'
    $p = Start-Process $exe -PassThru -RedirectStandardError $widgetLog -WorkingDirectory (Split-Path $exe)
    Log "запустив віджет pid $($p.Id), лог: $widgetLog"
    return @($p, $widgetLog)
}

Log "=== спостерігач стартував (pid $PID), віджет: $exe"
$lastAlive = Get-Date
while ($true) {
    $proc = Get-Process EdgeWidget -ErrorAction SilentlyContinue | Select-Object -First 1
    $widgetLog = $null
    if ($proc) {
        Log "підхопив запущений віджет pid $($proc.Id) ($($proc.Path)), працює з $($proc.StartTime)"
        $started = $proc.StartTime
    } elseif (Test-Path $exe) {
        $proc, $widgetLog = Start-Widget
        $started = Get-Date
    } else {
        Log "немає $exe — чекаю збірку"; Start-Sleep 30; continue
    }
    $null = $proc.Handle # keep a handle so the exit code stays readable

    while (-not $proc.WaitForExit(2000)) {
        Sample-Suspects
        if (((Get-Date) - $lastAlive).TotalMinutes -ge 10) {
            $proc.Refresh()
            Log ('живий pid {0}: пам''ять {1:N0} МБ, дескрипторів {2}' -f $proc.Id, ($proc.WorkingSet64 / 1MB), $proc.HandleCount)
            $lastAlive = Get-Date
        }
    }
    $result = Report-Exit $proc $widgetLog $started

    if ($result.Deliberate) {
        Log 'вихід через «Вийти» в треї: не перезапускаю, чекаю, поки запустиш сам'
        while (-not (Get-Process EdgeWidget -ErrorAction SilentlyContinue)) { Sample-Suspects; Start-Sleep 3 }
    } elseif (-not $result.Own -and $result.Code -eq 0) {
        Log 'звичайний вихід стороннього екземпляра: не перезапускаю, чекаю наступного запуску'
        while (-not (Get-Process EdgeWidget -ErrorAction SilentlyContinue)) { Sample-Suspects; Start-Sleep 3 }
    } else {
        Start-Sleep 3 # builds / installers: let them finish replacing the files
        while (Get-Process cmake, windeployqt, ISCC -ErrorAction SilentlyContinue) { Start-Sleep 3 }
    }
}
