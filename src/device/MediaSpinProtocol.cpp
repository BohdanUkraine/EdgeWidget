#include "device/MediaSpinProtocol.h"

namespace MediaSpin {
namespace {
enum WireType { Varint = 0, Fixed64 = 1, LengthDelimited = 2, Fixed32 = 5 };

void writeVarint(QByteArray &out, quint64 value)
{
    do {
        quint8 byte = value & 0x7F;
        value >>= 7;
        out.append(char(byte | (value ? 0x80 : 0)));
    } while (value);
}

bool readVarint(const QByteArray &in, int &pos, quint64 &value)
{
    value = 0;
    for (int shift = 0; pos < in.size() && shift < 64; shift += 7) {
        const quint8 byte = quint8(in[pos++]);
        value |= quint64(byte & 0x7F) << shift;
        if (!(byte & 0x80))
            return true;
    }
    return false;
}

void writeTag(QByteArray &out, int field, WireType type)
{
    writeVarint(out, quint64(field) << 3 | type);
}

void writeBytes(QByteArray &out, int field, const QByteArray &bytes)
{
    writeTag(out, field, LengthDelimited);
    writeVarint(out, quint64(bytes.size()));
    out.append(bytes);
}

template<typename OnField>
bool forEachField(const QByteArray &in, OnField onField)
{
    int pos = 0;
    while (pos < in.size()) {
        quint64 tag = 0;
        if (!readVarint(in, pos, tag))
            return false;
        const int field = int(tag >> 3);
        switch (WireType(tag & 0x7)) {
        case Varint: {
            quint64 value = 0;
            if (!readVarint(in, pos, value))
                return false;
            onField(field, value, QByteArray());
            break;
        }
        case LengthDelimited: {
            quint64 size = 0;
            if (!readVarint(in, pos, size) || pos + qint64(size) > in.size())
                return false;
            onField(field, 0, in.mid(pos, int(size)));
            pos += int(size);
            break;
        }
        case Fixed64: pos += 8; break;
        case Fixed32: pos += 4; break;
        default: return false;
        }
    }
    return pos == in.size();
}
}

QByteArray encode(const Message &m)
{
    QByteArray out;
    if (m.command != Command::Unknown) {
        writeTag(out, 1, Varint);
        writeVarint(out, quint64(m.command));
    }
    if (!m.data.isEmpty())
        writeBytes(out, 2, m.data.toUtf8());
    if (m.record) {
        QByteArray record;
        if (!m.record->uri.isEmpty())
            writeBytes(record, 1, m.record->uri.toUtf8());
        if (m.record->color) {
            writeTag(record, 2, Varint);
            writeVarint(record, m.record->color);
        }
        writeBytes(out, 3, record);
    }
    if (m.seq) {
        writeTag(out, 4, Varint);
        writeVarint(out, m.seq);
    }
    return out;
}

std::optional<Message> decode(const QByteArray &bytes)
{
    Message m;
    bool ok = forEachField(bytes, [&m](int field, quint64 value, const QByteArray &data) {
        if (field == 1)
            m.command = Command(value);
        else if (field == 2)
            m.data = QString::fromUtf8(data);
        else if (field == 3) {
            Record record;
            forEachField(data, [&record](int f, quint64 v, const QByteArray &d) {
                if (f == 1)
                    record.uri = QString::fromUtf8(d);
                else if (f == 2)
                    record.color = quint32(v);
            });
            m.record = record;
        } else if (field == 4)
            m.seq = quint32(value);
    });
    if (!ok)
        return std::nullopt;
    return m;
}

QString describe(const Message &m)
{
    static const char *names[] = { "UNKNOWN", "PAUSE", "RESUME", "GO_BACK", "GO_FORTH", "LOOP_ALBUM", "LOOP_SONG",
                                   "REMIX", "BIND", "MOVE_TO", "SET_VOLUME", "SWITCH_TO", "UNBIND", "TRACK_LEN",
                                   "ERROR", "ACK", "SET_MODE", "FADER_TOUCH", "REQUEST_SYNC" };
    const quint32 cmd = quint32(m.command);
    QString text = cmd < std::size(names) ? names[cmd] : QStringLiteral("CMD(%1)").arg(cmd);
    if (!m.data.isEmpty())
        text += QStringLiteral(" data=%1").arg(m.data);
    if (m.record)
        text += QStringLiteral(" record{%1, #%2}").arg(m.record->uri).arg(m.record->color, 6, 16, QLatin1Char('0'));
    return text;
}

}
