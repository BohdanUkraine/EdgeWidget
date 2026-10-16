#include "platform/Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace {
const QString kKey = QStringLiteral("EdgeWidget");

const QString kRunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");

QString command()
{
    return QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"');
}
} // namespace

namespace Autostart {

bool isEnabled()
{
    return QSettings(kRunKey, QSettings::NativeFormat).contains(kKey);
}

void setEnabled(bool enabled)
{
    QSettings key(kRunKey, QSettings::NativeFormat);
    if (enabled)
        key.setValue(kKey, command());
    else
        key.remove(kKey);
}

void refresh()
{
    QSettings key(kRunKey, QSettings::NativeFormat);
    if (key.contains(kKey) && key.value(kKey).toString() != command())
        key.setValue(kKey, command());
}

} // namespace Autostart
