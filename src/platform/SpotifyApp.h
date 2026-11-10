#pragma once

#include <QStringList>

namespace SpotifyApp {
bool isRunning();
bool launchQuietly();
QStringList dataDirs(); // where the desktop app may keep Users/<id>-user/
}
