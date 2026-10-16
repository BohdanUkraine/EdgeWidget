#pragma once

#include <QObject>
#include <QTimer>

// Starts the Spotify desktop app quietly (minimized, no window) when something has to
// play and no Spotify is running. While it starts up, isLaunching() is true so the UI can
// say so; whoever sees Spotify come alive calls finished().
class SpotifyLauncher : public QObject
{
    Q_OBJECT

public:
    static SpotifyLauncher &instance();

    bool isLaunching() const { return m_launching; }
    static bool isRunning();

    // Returns false if Spotify could not be started at all.
    bool launch();
    void finished();

signals:
    void launchingChanged(bool launching);
    void failed(const QString &message);

private:
    SpotifyLauncher();
    void setLaunching(bool launching);

    bool m_launching = false;
    QTimer m_timeout;
};
