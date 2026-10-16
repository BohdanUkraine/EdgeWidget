// Renders the vinyl icon for the exe / installer (.ico), the macOS bundle (.icns) and the
// Linux desktop entry (.png, 256 px):
//   make_icon <out.ico | out.icns | out.png>
// Built on demand (EXCLUDE_FROM_ALL), run by scripts\make-icon.ps1; the results are committed.

#include "ui/VinylIcon.h"

#include <QBuffer>
#include <QFile>
#include <QtEndian>

#include <cstdio>

namespace {

void put16(QByteArray &out, quint16 v)
{
    v = qToLittleEndian(v);
    out.append(reinterpret_cast<const char *>(&v), 2);
}

void put32(QByteArray &out, quint32 v)
{
    v = qToLittleEndian(v);
    out.append(reinterpret_cast<const char *>(&v), 4);
}

void put32be(QByteArray &out, quint32 v)
{
    v = qToBigEndian(v);
    out.append(reinterpret_cast<const char *>(&v), 4);
}

QByteArray png(int size)
{
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    vinylImage(size).convertToFormat(QImage::Format_ARGB32).save(&buf, "PNG");
    return data;
}

// PNG-compressed entries: supported by Windows Vista+ for every size.
QByteArray ico()
{
    const int sizes[] = { 16, 24, 32, 48, 64, 256 };
    QList<QByteArray> images;
    for (int size : sizes)
        images.append(png(size));

    QByteArray out;
    put16(out, 0); // reserved
    put16(out, 1); // type: icon
    put16(out, quint16(images.size()));
    quint32 offset = 6 + 16 * images.size();
    for (int i = 0; i < images.size(); ++i) {
        const int size = sizes[i];
        out.append(char(size >= 256 ? 0 : size)); // 0 means 256
        out.append(char(size >= 256 ? 0 : size));
        out.append(char(0)); // no palette
        out.append(char(0)); // reserved
        put16(out, 1);       // colour planes
        put16(out, 32);      // bits per pixel
        put32(out, quint32(images[i].size()));
        put32(out, offset);
        offset += images[i].size();
    }
    for (const QByteArray &image : images)
        out.append(image);
    return out;
}

// "icns" + total length, then (type, length, PNG) blocks, all big-endian.
QByteArray icns()
{
    const struct { const char *type; int size; } entries[] = {
        { "icp4", 16 }, { "icp5", 32 }, { "icp6", 64 }, { "ic07", 128 }, { "ic08", 256 },
        { "ic09", 512 }, { "ic10", 1024 }, { "ic11", 32 }, { "ic12", 64 }, { "ic13", 256 }, { "ic14", 512 },
    };
    QByteArray body;
    for (const auto &e : entries) {
        const QByteArray data = png(e.size);
        body.append(e.type, 4);
        put32be(body, quint32(8 + data.size()));
        body.append(data);
    }
    QByteArray out("icns", 4);
    put32be(out, quint32(8 + body.size()));
    return out + body;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: make_icon <out.ico | out.icns | out.png>\n");
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    const QByteArray data = path.endsWith(QLatin1String(".icns")) ? icns()
                          : path.endsWith(QLatin1String(".png"))  ? png(256)
                                                                  : ico();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
