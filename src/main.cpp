#include "app/Localization.h"
#include "device/DeviceController.h"
#include "platform/AppInit.h"
#include "platform/Autostart.h"
#include "playback/MediaController.h"
#include "spotify/SpotifyClient.h"
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
    Localization::instance().apply();

    MediaController media;
    SpotifyClient spotify;
    media.setRemote(&spotify);
    DeviceController device(&media, &spotify);
    EdgePanel panel(&media, &spotify, &device);
    panel.show();

    TrayIcon tray(&panel);
    tray.show();

    qInfo("[app] started");
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] { qInfo("[app] quitting"); });
    return app.exec();
}
