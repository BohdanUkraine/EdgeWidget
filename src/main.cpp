#include "app/Localization.h"
#include "device/DeviceController.h"
#include "platform/AppInit.h"
#include "platform/Autostart.h"
#include "playback/MediaController.h"
#include "spotify/SpotifyClient.h"
#include "spotify/SpotifyLauncher.h"
#include "ui/EdgePanel.h"
#include "ui/TrayIcon.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    AppInit::beforeQApplication();
    QApplication app(argc, argv);
    QApplication::setOrganizationName("EdgeWidget");
    QApplication::setApplicationName("EdgeWidget");
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(TrayIcon::vinylIcon());

    Autostart::refresh();
    Localization::instance().apply(); // before any widget, so the first paint is already translated

    MediaController media;
    SpotifyClient spotify;
    media.setRemote(&spotify); // Web API fallback when nothing plays on this PC
    DeviceController device(&media, &spotify); // MediaSpin record player (BLE, or the emulator)
    EdgePanel panel(&media, &spotify, &device);
    panel.show();

    TrayIcon tray(&panel);
    tray.show();
    QObject::connect(&spotify, &SpotifyClient::error, &tray, &TrayIcon::notify);
    QObject::connect(&SpotifyLauncher::instance(), &SpotifyLauncher::failed, &tray, &TrayIcon::notify);

    qInfo("[app] started");
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] { qInfo("[app] quitting"); });
    return app.exec();
}
