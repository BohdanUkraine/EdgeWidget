#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace MediaSpin {
enum class Command : quint32 {
    Unknown = 0,
    Pause = 1,
    Resume = 2,
    GoBack = 3,
    GoForth = 4,
    LoopAlbum = 5,
    LoopSong = 6,
    Remix = 7,
    Bind = 8,
    MoveTo = 9,
    SetVolume = 10,
    SwitchTo = 11,
    Unbind = 12,
    TrackLen = 13,
    Error = 14,
    Ack = 15,
    SetMode = 16,
    FaderTouch = 17,
    RequestSync = 18,
    PlatterSpeed = 19, // percent
    PlatterTest = 20,  // seconds, "0" stops
};

struct Record
{
    QString uri;
    quint32 color = 0;
};

struct Message
{
    Command command = Command::Unknown;
    QString data;
    std::optional<Record> record;
    quint32 seq = 0;
};

QByteArray encode(const Message &message);
std::optional<Message> decode(const QByteArray &bytes);
QString describe(const Message &message);

}
