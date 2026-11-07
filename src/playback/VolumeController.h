#pragma once

#include <QElapsedTimer>
#include <QObject>

class SpotifyClient;

class VolumeController : public QObject
{
    Q_OBJECT

public:
    explicit VolumeController(SpotifyClient *spotify, QObject *parent = nullptr);

    void set(int percent);
    void step(int delta);
    void toggleMute();

signals:
    void spotifyVolumeChanged(int percent);

private:
    bool spotifyTarget() const;
    void process();
    void apply(int current);
    void setSpotifyLevel(int percent);
    void remember(int percent);

    SpotifyClient *m_spotify;
    bool m_viaMixer = false; // not signed in to Spotify: its volume in the OS mixer
    int m_level = -1;
    QElapsedTimer m_levelAt;
    int m_mutedFrom = -1; // Spotify has no mute: the level to come back to
    int m_pendingDelta = 0;
    bool m_pendingMute = false;
    bool m_fetching = false;
};
