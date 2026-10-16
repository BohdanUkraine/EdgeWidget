#include "ui/library/LibraryStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>

namespace LibraryStyle {
namespace {
constexpr int kTrackArt = 32;
constexpr int kCoverPx = 96; // cached cover size, HiDPI-friendly

QPolygonF playTriangle(const QPointF &c, qreal size)
{
    QPolygonF tri;
    tri << QPointF(c.x() - size * 0.55, c.y() - size) << QPointF(c.x() - size * 0.55, c.y() + size)
        << QPointF(c.x() + size, c.y());
    return tri;
}

QPainterPath artPath(const QRectF &r, const LibraryItem &item)
{
    QPainterPath path;
    if (item.kind == LibraryItem::Artist)
        path.addEllipse(r); // artists are round, like in Spotify
    else
        path.addRoundedRect(r, 4, 4);
    return path;
}

void paintArt(QPainter &p, const QFont &base, const QRectF &r, const LibraryItem &item, const QImage &cover)
{
    const QPainterPath path = artPath(r, item);

    if (item.kind == LibraryItem::Liked) {
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, QColor(69, 10, 245));
        g.setColorAt(1, QColor(196, 239, 217));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(path);
        const QPointF c = r.center();
        QPainterPath heart;
        heart.moveTo(c.x(), c.y() + 7);
        heart.cubicTo(c.x() - 11, c.y() - 1, c.x() - 6, c.y() - 10, c.x(), c.y() - 4);
        heart.cubicTo(c.x() + 6, c.y() - 10, c.x() + 11, c.y() - 1, c.x(), c.y() + 7);
        p.setBrush(Qt::white);
        p.drawPath(heart);
        return;
    }

    if (!cover.isNull()) {
        p.save();
        p.setClipPath(path, Qt::IntersectClip); // keep the caller's clip (scrolling lists)
        p.drawImage(r, cover);
        p.restore();
        return;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(th().track);
    p.drawPath(path);
    QFont f = base;
    f.setPixelSize(18);
    p.setFont(f);
    p.setPen(th().subText);
    p.drawText(r, Qt::AlignCenter, QStringLiteral("♪"));
}

// Small vinyl with NFC waves: "write this to a record".
void paintNfcButton(QPainter &p, const QRect &r, bool hovered)
{
    const QColor color = hovered ? th().accent : th().subText;
    p.save();
    QColor tint = th().accent;
    tint.setAlpha(40);
    p.setPen(QPen(hovered ? th().accent : th().line, 1));
    p.setBrush(hovered ? tint : th().hover);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    p.translate(QRectF(r).center());
    const QPointF disc(-3, 0);
    p.setPen(QPen(color, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(disc, 5.5, 5.5);
    p.setPen(QPen(color, 0.8));
    p.drawEllipse(disc, 3.2, 3.2);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(disc, 1.2, 1.2);

    p.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    for (qreal radius : { 5.0, 8.0 }) {
        const QRectF arc(disc.x() + 2 - radius, -radius, 2 * radius, 2 * radius);
        p.drawArc(arc, -40 * 16, 80 * 16);
    }
    p.restore();
}
} // namespace

QRectF artRect(const QRect &row)
{
    return QRectF(row.x() + 2, row.y() + (kRowH - kArt) / 2.0, kArt, kArt);
}

QRect nfcRect(const QRect &row)
{
    return QRect(row.right() - 8 - kNfcSize, row.y() + (kRowH - kNfcSize) / 2, kNfcSize, kNfcSize);
}

RowPart partAt(const QRect &row, const QPoint &pos)
{
    if (!row.contains(pos))
        return RowPart::None;
    if (artRect(row).contains(pos))
        return RowPart::Art;
    if (nfcRect(row).adjusted(-3, -3, 3, 3).contains(pos))
        return RowPart::Nfc;
    return RowPart::Text;
}

void paintRow(QPainter &p, const QFont &base, const QRect &row, const LibraryItem &item, const QImage &cover,
              const RowState &state)
{
    if (state.hovered) {
        p.setPen(Qt::NoPen);
        p.setBrush(th().hover);
        p.drawRoundedRect(QRectF(row).adjusted(0, 1, -6, -1), 6, 6);
    }

    const QRectF art = artRect(row);
    paintArt(p, base, art, item, cover);

    // The playing item always shows "pause"; others show "play" when the cover is hovered.
    const bool artHovered = state.hovered && state.hoverPart == RowPart::Art;
    const bool playingHere = state.active && state.playing;
    if (playingHere || artHovered) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, artHovered ? 140 : 110));
        p.drawPath(artPath(art, item));
        const QPointF c = art.center();
        p.setBrush(playingHere ? th().accent : QColor(Qt::white)); // over the cover image
        if (playingHere) {
            p.drawRoundedRect(QRectF(c.x() - 5.5, c.y() - 7, 4, 14), 1, 1);
            p.drawRoundedRect(QRectF(c.x() + 1.5, c.y() - 7, 4, 14), 1, 1);
        } else {
            p.drawPolygon(playTriangle(c, 7));
        }
    }

    const QRect nfc = nfcRect(row);
    paintNfcButton(p, nfc, state.hovered && state.hoverPart == RowPart::Nfc);

    const int textX = int(art.right()) + 10;
    const int textW = nfc.left() - 8 - textX;
    QFont title = base;
    title.setPixelSize(13);
    title.setBold(true);
    title.setUnderline(state.hovered && state.hoverPart == RowPart::Text);
    QFont sub = base;
    sub.setPixelSize(11);

    QString kind;
    switch (item.kind) {
    case LibraryItem::Liked:
    case LibraryItem::Playlist: kind = QObject::tr("Плейлист"); break;
    case LibraryItem::Album: kind = QObject::tr("Альбом"); break;
    case LibraryItem::Artist: kind = QObject::tr("Виконавець"); break;
    }
    const QString subtitle = item.owner.isEmpty() ? kind : kind + QStringLiteral(" • ") + item.owner;

    p.setFont(title);
    p.setPen(state.active ? th().accent : th().text);
    // Liked Songs is named by us, so it follows the UI language.
    const QString name = item.kind == LibraryItem::Liked ? QObject::tr("Улюблені треки") : item.name;
    p.drawText(QRect(textX, row.y() + 4, textW, 19), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(title).elidedText(name, Qt::ElideRight, textW));
    p.setFont(sub);
    p.setPen(th().subText);
    p.drawText(QRect(textX, row.y() + 23, textW, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(sub).elidedText(subtitle, Qt::ElideRight, textW));
}

void paintTrack(QPainter &p, const QFont &base, const QRect &row, const QImage &cover, const Track &track,
                bool hovered, bool current, bool playing)
{
    if (hovered) {
        p.setPen(Qt::NoPen);
        p.setBrush(th().hover);
        p.drawRoundedRect(QRectF(row).adjusted(0, 1, -6, -1), 6, 6);
    }

    QFont title = base;
    title.setPixelSize(13);
    QFont sub = base;
    sub.setPixelSize(11);

    // Small cover; "play" over it on hover, an equalizer while this track plays.
    const QRectF art(row.x() + 4, row.y() + (kTrackH - kTrackArt) / 2.0, kTrackArt, kTrackArt);
    QPainterPath artPath;
    artPath.addRoundedRect(art, 3, 3);
    if (!cover.isNull()) {
        p.save();
        p.setClipPath(artPath, Qt::IntersectClip); // keep the caller's clip (scrolling lists)
        p.drawImage(art, cover);
        p.restore();
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(th().track);
        p.drawPath(artPath);
    }
    if (hovered || (current && playing)) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 130));
        p.drawPath(artPath);
        if (hovered) {
            p.setBrush(Qt::white); // over the cover image
            p.drawPolygon(playTriangle(art.center(), 5));
        } else {
            p.setBrush(th().accent);
            const qreal heights[] = { 8, 13, 6 };
            for (int b = 0; b < 3; ++b)
                p.drawRect(QRectF(art.center().x() - 5 + b * 4, art.center().y() + 6 - heights[b], 2.5, heights[b]));
        }
    }
    const int durW = 40;
    const int textX = int(art.right()) + 10;
    const int textW = row.right() - 10 - durW - textX;
    p.setFont(title);
    p.setPen(current ? th().accent : th().text);
    p.drawText(QRect(textX, row.y() + 3, textW, 19), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(title).elidedText(track.name, Qt::ElideRight, textW));
    p.setFont(sub);
    p.setPen(th().subText);
    p.drawText(QRect(textX, row.y() + 21, textW, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(sub).elidedText(track.artists, Qt::ElideRight, textW));
    p.drawText(QRect(row.right() - 10 - durW, row.y(), durW, kTrackH), Qt::AlignRight | Qt::AlignVCenter,
               formatTime(track.durationMs));
}

void paintScrollbar(QPainter &p, const QRectF &track, int contentH, int scroll, int maxScroll)
{
    if (maxScroll <= 0 || contentH <= 0)
        return;
    const qreal h = std::max(18.0, track.height() * track.height() / contentH);
    const qreal y = track.top() + (track.height() - h) * scroll / maxScroll;
    p.setPen(Qt::NoPen);
    QColor bar = th().subText;
    bar.setAlpha(110);
    p.setBrush(bar);
    p.drawRoundedRect(QRectF(track.right() - 3, y, 3, h), 1.5, 1.5);
}

void paintEdgeFades(QPainter &p, const QRectF &area, int scroll, int maxScroll)
{
    for (bool top : { true, false }) {
        if ((top && scroll == 0) || (!top && scroll >= maxScroll))
            continue;
        const QRectF band = top ? QRectF(area.left(), area.top(), area.width(), 14)
                                : QRectF(area.left(), area.bottom() - 13, area.width(), 14);
        QLinearGradient g(band.topLeft(), band.bottomLeft());
        g.setColorAt(top ? 0 : 1, th().fade(235));
        g.setColorAt(top ? 1 : 0, th().fade(0));
        p.fillRect(band, g);
    }
}

void ScrollGesture::press(int y)
{
    pressed = true;
    dragged = false;
    m_pressY = y;
    m_pressOffset = offset;
}

bool ScrollGesture::drag(int y, int maxOffset)
{
    constexpr int kThreshold = 5;
    if (!pressed)
        return false;
    if (std::abs(y - m_pressY) > kThreshold)
        dragged = true;
    if (dragged)
        offset = std::clamp(m_pressOffset - (y - m_pressY), 0, maxOffset);
    return dragged;
}

bool ScrollGesture::release()
{
    const bool wasDrag = dragged;
    pressed = dragged = false;
    return wasDrag;
}

void ScrollGesture::wheel(const QWheelEvent *event, int step, int maxOffset)
{
    const QPoint px = event->pixelDelta();
    const int delta = !px.isNull() ? px.y() : event->angleDelta().y() * step / 120;
    offset = std::clamp(offset - delta, 0, maxOffset);
}

void ScrollGesture::clamp(int maxOffset)
{
    offset = std::clamp(offset, 0, maxOffset);
}

QString formatTime(qint64 ms)
{
    const qint64 s = std::max<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString nfcTooltip()
{
    return QObject::tr("Записати на NFC-платівку");
}

} // namespace LibraryStyle

CoverCache::CoverCache(SpotifyClient *spotify, QObject *parent)
    : QObject(parent)
    , m_spotify(spotify)
{
}

QImage CoverCache::get(const QString &url)
{
    if (url.isEmpty())
        return {};
    const auto it = m_covers.constFind(url);
    if (it != m_covers.constEnd())
        return *it;
    if (!m_requested.contains(url)) {
        m_requested.insert(url);
        m_spotify->fetchImage(url, this, [this, url](const QImage &img) {
            if (img.isNull())
                return;
            m_covers.insert(url, img.scaled(LibraryStyle::kCoverPx, LibraryStyle::kCoverPx,
                                            Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            emit coverReady();
        });
    }
    return {};
}
