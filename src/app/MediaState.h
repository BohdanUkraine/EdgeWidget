#pragma once

#include <QImage>
#include <QString>

#include <algorithm>
#include <chrono>

struct MediaState
{
    bool available = false;
    QString title;
    QString artist;
    QString album;
    QImage cover;
    bool playing = false;
    bool shuffle = false;
    int repeat = 0;
    qint64 positionMs = 0;
    qint64 durationMs = 0;
    qint64 sampledAtMs = 0;
    bool remote = false;
    QString deviceName;

    static qint64 nowMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    qint64 positionNow() const
    {
        const qint64 pos = positionMs + (playing ? nowMs() - sampledAtMs : 0);
        return std::clamp<qint64>(pos, 0, std::max<qint64>(durationMs, 0));
    }

    QString trackKey() const { return title + '\n' + artist + '\n' + album; }
};
