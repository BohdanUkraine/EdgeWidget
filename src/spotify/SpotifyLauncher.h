#pragma once

#include <QObject>
#include <QTimer>

class SpotifyLauncher : public QObject
{
    Q_OBJECT

public:
    static SpotifyLauncher &instance();

    bool isLaunching() const { return m_launching; }
    static bool isRunning();

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
