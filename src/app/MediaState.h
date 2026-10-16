#pragma once

#include <QImage>
#include <QString>

#include <algorithm>
#include <chrono>

// What is playing right now, from the local player (Windows media session) or from the
// Spotify Web API when the music plays on another device.
struct MediaState
{
    bool available = false; // there is something to show / control
    QString title;
    QString artist;
    QString album;
    QImage cover;
    bool playing = false;
    bool shuffle = false;
    int repeat = 0; // 0 = off, 1 = track, 2 = list
    qint64 positionMs = 0;
    qint64 durationMs = 0;
    qint64 sampledAtMs = 0; // steady clock time when positionMs was valid
    bool remote = false;    // playing on another device, controlled through the Web API
    QString deviceName;

    static qint64 nowMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    // Position extrapolated to "now" while playing.
    qint64 positionNow() const
    {
        const qint64 pos = positionMs + (playing ? nowMs() - sampledAtMs : 0);
        return std::clamp<qint64>(pos, 0, std::max<qint64>(durationMs, 0));
    }

    QString trackKey() const { return title + '\n' + artist + '\n' + album; }
};
