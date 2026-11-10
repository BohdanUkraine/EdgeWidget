#include "spotify/SpotifyLocalLibrary.h"

#include "platform/SpotifyApp.h"

#include <QDir>
#include <QFile>
#include <QHash>

#include <algorithm>
#include <cstring>

// LevelDB layout: https://github.com/google/leveldb/blob/main/doc/table_format.md
// Only sorted tables (*.ldb) are read: the newest changes sit in the *.log, which the running app
// keeps locked, and reach the tables once it compacts them.
namespace {
const QByteArray kPinPrefix = QByteArrayLiteral("!col#ylpin#");

bool varint(const QByteArray &b, int &i, quint64 &out)
{
    out = 0;
    for (int shift = 0; i < b.size() && shift < 64; shift += 7) {
        const quint8 c = quint8(b[i++]);
        out |= quint64(c & 0x7F) << shift;
        if (c < 0x80)
            return true;
    }
    return false;
}

QByteArray snappy(const QByteArray &in)
{
    int i = 0;
    quint64 length = 0;
    if (!varint(in, i, length) || length > 64 * 1024 * 1024)
        return {};
    QByteArray out;
    out.reserve(int(length));
    while (i < in.size()) {
        const quint8 tag = quint8(in[i++]);
        quint64 len = 0;
        quint64 offset = 0;
        int extra = 0;
        switch (tag & 3) {
        case 0:
            len = tag >> 2;
            if (len >= 60) {
                const int bytes = int(len) - 59;
                if (i + bytes > in.size())
                    return {};
                len = 0;
                for (int k = 0; k < bytes; ++k)
                    len |= quint64(quint8(in[i + k])) << (8 * k);
                i += bytes;
            }
            ++len;
            if (i + qint64(len) > in.size())
                return {};
            out.append(in.constData() + i, int(len));
            i += int(len);
            continue;
        case 1:
            len = ((tag >> 2) & 7) + 4;
            extra = 1;
            break;
        case 2:
            len = (tag >> 2) + 1;
            extra = 2;
            break;
        default:
            len = (tag >> 2) + 1;
            extra = 4;
            break;
        }
        if (i + extra > in.size())
            return {};
        for (int k = 0; k < extra; ++k)
            offset |= quint64(quint8(in[i + k])) << (8 * k);
        if ((tag & 3) == 1)
            offset = (quint64(tag >> 5) << 8) | quint8(in[i]);
        i += extra;
        if (offset == 0 || offset > quint64(out.size()))
            return {};
        const int start = out.size() - int(offset);
        for (quint64 k = 0; k < len; ++k)
            out.append(out[start + int(k)]); // copies may overlap what they write
    }
    return quint64(out.size()) == length ? out : QByteArray();
}

QByteArray readBlock(QFile &file, quint64 offset, quint64 size)
{
    if (!file.seek(qint64(offset)) || size > 64 * 1024 * 1024)
        return {};
    const QByteArray raw = file.read(qint64(size) + 1); // + compression type byte
    if (quint64(raw.size()) != size + 1)
        return {};
    const char type = raw.back();
    if (type == 0)
        return raw.left(int(size));
    return type == 1 ? snappy(raw.left(int(size))) : QByteArray();
}

template<typename F>
void forEachEntry(const QByteArray &block, F f)
{
    if (block.size() < 4)
        return;
    quint32 restarts = 0;
    std::memcpy(&restarts, block.constData() + block.size() - 4, 4);
    const qint64 end = qint64(block.size()) - 4 - 4 * qint64(restarts);
    QByteArray key;
    int i = 0;
    while (i < end) {
        quint64 shared, own, valueLen;
        if (!varint(block, i, shared) || !varint(block, i, own) || !varint(block, i, valueLen)
            || shared > quint64(key.size()) || i + qint64(own + valueLen) > end)
            return;
        key = key.left(int(shared)) + block.mid(i, int(own));
        i += int(own);
        f(key, block.mid(i, int(valueLen)));
        i += int(valueLen);
    }
}

struct Entry
{
    quint64 seq = 0;
    bool live = false;
    QByteArray value;
};

// Pin value: protobuf { 1: uri, 2: pinned at (unix seconds) }.
quint64 pinnedAt(const QByteArray &value)
{
    int i = 0;
    quint64 tag;
    while (varint(value, i, tag)) {
        quint64 n = 0;
        if ((tag & 7) == 0) {
            if (!varint(value, i, n))
                return 0;
            if (tag >> 3 == 2)
                return n;
        } else if ((tag & 7) == 2) {
            if (!varint(value, i, n) || i + qint64(n) > value.size())
                return 0;
            i += int(n);
        } else {
            return 0;
        }
    }
    return 0;
}

// Rootlist value: protobuf with the items' URIs as length-prefixed strings, in list order.
QStringList uris(const QByteArray &value)
{
    static const QByteArray kMark = QByteArrayLiteral("spotify:");
    QStringList out;
    for (qsizetype i = value.indexOf(kMark); i > 0; i = value.indexOf(kMark, i + 1)) {
        // the length varint right before: one byte, or two when the second-last has the high bit
        qsizetype len = quint8(value[i - 1]);
        if (len >= 0x80)
            continue;
        if (i >= 2 && quint8(value[i - 2]) >= 0x80)
            len = (quint8(value[i - 2]) & 0x7F) | (len << 7);
        if (i + len > value.size())
            continue;
        const QString uri = QString::fromUtf8(value.mid(i, len));
        if (uri.startsWith(QLatin1String("spotify:playlist:")) || uri.startsWith(QLatin1String("spotify:start-group:"))
            || uri.startsWith(QLatin1String("spotify:end-group:")))
            out.append(uri);
    }
    return out;
}

// Keys starting with `prefix`, newest version of each: only the blocks whose key range can hold them.
void scanTable(const QString &path, const QByteArray &prefix, QHash<QByteArray, Entry> &found)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 48 || !file.seek(file.size() - 48))
        return;
    const QByteArray footer = file.read(48);
    int i = 0;
    quint64 metaOffset, metaSize, indexOffset, indexSize;
    if (!varint(footer, i, metaOffset) || !varint(footer, i, metaSize) || !varint(footer, i, indexOffset)
        || !varint(footer, i, indexSize))
        return;

    const QByteArray prefixEnd = prefix + char(0xFF);
    QByteArray previous;
    forEachEntry(readBlock(file, indexOffset, indexSize), [&](const QByteArray &last, const QByteArray &handle) {
        // block holds the keys after the previous block's last one, up to its own last
        const bool needed = last >= prefix && (previous.isEmpty() || previous < prefixEnd);
        previous = last;
        if (!needed)
            return;
        int k = 0;
        quint64 offset, size;
        if (!varint(handle, k, offset) || !varint(handle, k, size))
            return;
        forEachEntry(readBlock(file, offset, size), [&](const QByteArray &key, const QByteArray &value) {
            if (key.size() < 8 || !key.startsWith(prefix))
                return;
            quint64 tag = 0;
            std::memcpy(&tag, key.constData() + key.size() - 8, 8);
            const QByteArray userKey = key.left(key.size() - 8);
            Entry &e = found[userKey];
            if (tag >> 8 >= e.seq) {
                e.seq = tag >> 8;
                e.live = (tag & 0xFF) == 1;
                e.value = value;
            }
        });
    });
}
}

namespace SpotifyLocalLibrary {
Data read(const QString &userId)
{
    Data data;
    if (userId.isEmpty())
        return data;
    for (const QString &dir : SpotifyApp::dataDirs()) {
        const QDir db(dir + "/Users/" + userId + "-user/primary.ldb");
        if (!db.exists())
            continue;
        data.found = true;
        QHash<QByteArray, Entry> pins;
        for (const QString &name : db.entryList({ QStringLiteral("*.ldb") }, QDir::Files, QDir::Name))
            scanTable(db.filePath(name), kPinPrefix, pins);
        // "!pl#slc#" <length byte> "spotify:user:<id>:rootlist" "#"
        const QByteArray rootUri = "spotify:user:" + userId.toUtf8() + ":rootlist";
        const QByteArray rootKey = "!pl#slc#" + QByteArray(1, char(rootUri.size())) + rootUri + '#';
        QHash<QByteArray, Entry> root;
        for (const QString &name : db.entryList({ QStringLiteral("*.ldb") }, QDir::Files, QDir::Name))
            scanTable(db.filePath(name), rootKey, root);
        const auto rootEntry = root.constFind(rootKey);
        if (rootEntry != root.cend() && rootEntry->live)
            data.rootlist = uris(rootEntry->value);

        QList<QPair<quint64, QString>> ordered;
        for (auto it = pins.cbegin(); it != pins.cend(); ++it) {
            // "!col#ylpin#" <length byte> <uri> "#"
            const QByteArray &key = it.key();
            if (!it->live || key.size() <= kPinPrefix.size() + 1)
                continue;
            const int len = quint8(key[kPinPrefix.size()]);
            const QByteArray uri = key.mid(kPinPrefix.size() + 1, len);
            if (uri.size() == len && uri.startsWith("spotify:"))
                ordered.append({ pinnedAt(it->value), QString::fromUtf8(uri) });
        }
        std::sort(ordered.begin(), ordered.end());
        for (const auto &pin : ordered)
            data.pinned.append(pin.second);
        break;
    }
    return data;
}
}
