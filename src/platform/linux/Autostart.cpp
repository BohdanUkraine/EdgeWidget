#include "platform/Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
QString desktopFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/edgewidget.desktop");
}

QByteArray entry()
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString exe = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    return QStringLiteral(
               "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=Edge Widget\n"
               "Exec=\"%1\"\n"
               "Icon=edgewidget\n"
               "X-GNOME-Autostart-enabled=true\n")
        .arg(exe)
        .toUtf8();
}
}

namespace Autostart {
bool isEnabled()
{
    return QFile::exists(desktopFile());
}

void setEnabled(bool enabled)
{
    if (!enabled) {
        QFile::remove(desktopFile());
        return;
    }
    QDir().mkpath(QFileInfo(desktopFile()).absolutePath());
    QFile file(desktopFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(entry());
}

void refresh()
{
    QFile file(desktopFile());
    if (file.open(QIODevice::ReadOnly) && file.readAll() != entry()) {
        file.close();
        setEnabled(true);
    }
}

}
