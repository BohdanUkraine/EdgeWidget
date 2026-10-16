#include "platform/Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
const QString kLabel = QStringLiteral("com.edgewidget.EdgeWidget");

QString plistPath()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/") + kLabel + QStringLiteral(".plist");
}

// A LaunchAgent: launchd starts the app at login. Points straight at the bundle's executable.
QByteArray plist()
{
    const QString exe = QCoreApplication::applicationFilePath().toHtmlEscaped();
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\">\n<dict>\n"
               "  <key>Label</key><string>%1</string>\n"
               "  <key>ProgramArguments</key><array><string>%2</string></array>\n"
               "  <key>RunAtLoad</key><true/>\n"
               "  <key>ProcessType</key><string>Interactive</string>\n"
               "</dict>\n</plist>\n")
        .arg(kLabel, exe)
        .toUtf8();
}
} // namespace

namespace Autostart {

bool isEnabled()
{
    return QFile::exists(plistPath());
}

void setEnabled(bool enabled)
{
    if (!enabled) {
        QFile::remove(plistPath());
        return;
    }
    QDir().mkpath(QFileInfo(plistPath()).absolutePath());
    QFile file(plistPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(plist());
}

void refresh()
{
    QFile file(plistPath());
    if (file.open(QIODevice::ReadOnly) && file.readAll() != plist()) {
        file.close();
        setEnabled(true);
    }
}

} // namespace Autostart
