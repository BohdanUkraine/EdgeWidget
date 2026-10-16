#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// MediaSpin record player protocol (shared/PROTOCOL.md, shared/proto/api/request.proto).
// The message is tiny, so it is encoded by hand instead of pulling in libprotobuf.
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
};

// What is written on a physical record.
struct Record
{
    QString uri;
    quint32 color = 0; // 0xRRGGBB
};

struct Message
{
    Command command = Command::Unknown;
    QString data;
    std::optional<Record> record;
};

QByteArray encode(const Message &message);
std::optional<Message> decode(const QByteArray &bytes);
QString describe(const Message &message); // for logs

// TCP framing used with the emulator: varint length + message bytes.
QByteArray frame(const Message &message);

class FrameReader
{
public:
    QList<Message> feed(const QByteArray &bytes);

private:
    QByteArray m_buffer;
};

} // namespace MediaSpin
