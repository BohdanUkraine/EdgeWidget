#include "platform/SpotifyApp.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

namespace {
// Desktop app from spotify.com installs per user here.
QString desktopExe()
{
    const QString path = QDir(qEnvironmentVariable("APPDATA")).filePath("Spotify/Spotify.exe");
    return QFileInfo::exists(path) ? path : QString();
}
} // namespace

namespace SpotifyApp {

bool isRunning()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found; ok = Process32NextW(snapshot, &entry))
        found = _wcsicmp(entry.szExeFile, L"Spotify.exe") == 0;
    CloseHandle(snapshot);
    return found;
}

bool launchQuietly()
{
    const QString exe = desktopExe();
    if (!exe.isEmpty()) // the same switch Spotify uses for "start with Windows": no window, just runs
        return QProcess::startDetached(exe, { QStringLiteral("--minimized") });
    // Microsoft Store version: can only be activated normally (its window shows).
    return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                   { QStringLiteral("shell:AppsFolder\\SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify") });
}

} // namespace SpotifyApp
