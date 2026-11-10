#include "ui/library/LibraryView.h"

#include "ui/SpotifySetup.h"

#include <QMouseEvent>
#include <QToolTip>

#include <algorithm>

using namespace LibraryStyle;

namespace {
constexpr int kDetailAnimMs = 260;
constexpr int kRevealScrollMs = 240;
constexpr int kSlideMs = 260;
}

LibraryView::LibraryView(SpotifyClient *spotify, CoverCache *covers, QWidget *parent)
    : ScaledWidget(parent)
    , m_spotify(spotify)
    , m_covers(covers)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    m_detailAnim.setDuration(kDetailAnimMs);
    m_detailAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_detailAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_detailT = v.toReal();
        update();
    });
    connect(&m_detailAnim, &QVariantAnimation::finished, this, [this] {
        if (!m_detailOpen) {
            m_detail = -1;
            update();
        }
    });

    m_slideAnim.setDuration(kSlideMs);
    m_slideAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_slideAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_slideT = v.toReal();
        update();
    });

    m_scrollAnim.setDuration(kRevealScrollMs);
    m_scrollAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_scrollAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_scroll.offset = v.toInt();
        update();
    });
    connect(&m_scrollAnim, &QVariantAnimation::finished, this, [this] {
        const auto &lib = m_spotify->library();
        const auto it = std::find_if(lib.begin(), lib.end(), [this](const LibraryItem &i) { return i.uri == m_revealUri; });
        m_revealUri.clear();
        if (it != lib.end())
            openDetail(int(it - lib.begin()));
    });

    connect(m_spotify, &SpotifyClient::libraryChanged, this, [this] {
        m_hover = {};
        // a folder that is gone (emptied, or no longer in the cache): back to what still exists
        while (!m_path.isEmpty() && pathTo(m_path.last()) != m_path) {
            m_path.removeLast();
            m_scroll.offset = m_pathOffsets.takeLast();
        }
        emit pageChanged(); // also covers a renamed folder
        m_scroll.clamp(maxScroll());
        if (m_detail >= 0) {
            const auto &lib = m_spotify->library();
            const auto it = std::find_if(lib.begin(), lib.end(),
                                         [this](const LibraryItem &i) { return i.uri == m_detailUri; });
            if (it == lib.end())
                closeDetail();
            else
                m_detail = int(it - lib.begin());
        }
        update();
    });
    connect(m_spotify, &SpotifyClient::activeChanged, this, qOverload<>(&QWidget::update));
    connect(m_spotify, &SpotifyClient::connectedChanged, this, qOverload<>(&QWidget::update));
    connect(m_covers, &CoverCache::coverReady, this, qOverload<>(&QWidget::update));
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
        m_scroll.clamp(maxScroll());
        update();
    });
}

void LibraryView::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    update();
}

QList<int> LibraryView::rows(const QString &folder) const
{
    QList<int> out;
    const auto &lib = m_spotify->library();
    for (int i = 0; i < lib.size(); ++i) {
        if (lib.at(i).folder == folder && passes(lib.at(i)))
            out.append(i);
    }
    return out;
}

bool LibraryView::passes(const LibraryItem &item) const
{
    switch (m_filter) {
    case Filter::All: return true;
    case Filter::Playlists:
        return item.kind == LibraryItem::Playlist || item.kind == LibraryItem::Liked
            || item.kind == LibraryItem::Folder;
    case Filter::Albums: return item.kind == LibraryItem::Album;
    case Filter::Artists: return item.kind == LibraryItem::Artist;
    }
    return true;
}

QString LibraryView::folderTitle() const
{
    const QString f = folder();
    for (const LibraryItem &i : m_spotify->library()) {
        if (!f.isEmpty() && i.uri == f)
            return i.name;
    }
    return {};
}

void LibraryView::setFilter(Filter filter)
{
    if (filter == m_filter)
        return;
    m_filter = filter;
    m_hover = {};
    m_scrollAnim.stop();
    m_scroll = {};
    update();
    emit filterChanged();
}

bool LibraryView::contains(const QString &folder, const QString &uri) const
{
    if (uri.isEmpty())
        return false;
    for (const LibraryItem &i : m_spotify->library()) {
        if (i.uri == uri)
            return pathTo(i.folder).contains(folder);
    }
    return false;
}

// Folders from the top level down to `folder`; empty when it is not in the library.
QStringList LibraryView::pathTo(const QString &folder) const
{
    QStringList path;
    QString f = folder;
    while (!f.isEmpty() && path.size() < 32) {
        const auto &lib = m_spotify->library();
        const auto it = std::find_if(lib.begin(), lib.end(), [&f](const LibraryItem &i) {
            return i.uri == f && i.kind == LibraryItem::Folder;
        });
        if (it == lib.end())
            return {};
        path.prepend(f);
        f = it->folder;
    }
    return path;
}

QRect LibraryView::rowRect(int position, int offset) const
{
    return QRect(0, position * kRowH - offset, int(lw()), kRowH);
}

int LibraryView::maxScroll() const
{
    return std::max(0, int(rows(folder()).size()) * kRowH - int(lh()));
}

void LibraryView::openDetail(int index)
{
    const LibraryItem item = m_spotify->library().at(index);
    const int position = int(rows(folder()).indexOf(index));
    m_detail = index;
    m_detailUri = item.uri;
    m_detailOpen = true;
    m_hover = {};
    m_detailAnim.stop();
    m_detailAnim.setStartValue(m_detailT);
    m_detailAnim.setEndValue(1.0);
    m_detailAnim.start();
    const int top = position >= 0 ? rowRect(position, m_scroll.offset).top() : 0;
    emit detailOpened(item, mapToGlobal(QPointF(0, top * uiScale()).toPoint()).y());
}

void LibraryView::closeDetail(bool fadeHeader)
{
    if (!m_detailOpen)
        return;
    m_detailOpen = false;
    m_hover = {};
    m_detailAnim.stop();
    m_detailAnim.setStartValue(m_detailT);
    m_detailAnim.setEndValue(0.0);
    m_detailAnim.start();
    emit detailClosed(fadeHeader);
}

void LibraryView::slide(const QString &fromFolder, int fromOffset, bool forward)
{
    m_slideFrom = fromFolder;
    m_slideFromOffset = fromOffset;
    m_slideForward = forward;
    m_hover = {};
    m_slideAnim.stop();
    m_slideAnim.setStartValue(0.0);
    m_slideAnim.setEndValue(1.0);
    m_slideAnim.start();
    emit pageChanged();
}

void LibraryView::openFolder(const QString &uri)
{
    const QString from = folder();
    const int fromOffset = m_scroll.offset;
    m_path.append(uri);
    m_pathOffsets.append(fromOffset);
    m_scroll = {};
    slide(from, fromOffset, true);
}

void LibraryView::goBack()
{
    if (m_path.isEmpty())
        return;
    const QString from = folder();
    const int fromOffset = m_scroll.offset;
    m_path.removeLast();
    m_scroll = {};
    m_scroll.offset = m_pathOffsets.takeLast();
    m_scroll.clamp(maxScroll());
    slide(from, fromOffset, false);
}

bool LibraryView::reveal(const QString &uri)
{
    const auto &lib = m_spotify->library();
    const auto it = std::find_if(lib.begin(), lib.end(), [&uri](const LibraryItem &i) { return i.uri == uri; });
    if (uri.isEmpty() || it == lib.end())
        return false;
    const int index = int(it - lib.begin());
    if (!passes(*it))
        setFilter(Filter::All);
    const QStringList path = pathTo(it->folder);
    if (path != m_path) {
        // straight to its folder: the way back still goes up level by level
        m_path = path;
        m_pathOffsets = QList<int>(path.size(), 0);
        m_scroll = {};
        m_slideAnim.stop();
        m_slideT = 1.0;
        emit pageChanged();
    }
    if (m_detailOpen) {
        if (m_detailUri != uri)
            openDetail(index); // the list is covered: just switch what the popup shows
        return true;
    }
    const int top = int(rows(folder()).indexOf(index)) * kRowH;
    const int listH = int(lh());
    int target = m_scroll.offset;
    if (top < m_scroll.offset || top + kRowH > m_scroll.offset + listH)
        target = std::clamp(top - (listH - kRowH) / 2, 0, maxScroll());
    m_revealUri = uri;
    m_scrollAnim.stop();
    m_scrollAnim.setStartValue(m_scroll.offset);
    m_scrollAnim.setEndValue(target);
    m_scrollAnim.start();
    return true;
}

LibraryView::Hover LibraryView::hitTest(const QPoint &pos) const
{
    if (!m_spotify->isConnected() || m_detail >= 0 || m_slideT < 1.0 || !logicalRect().contains(pos))
        return {};
    Hover h;
    const QList<int> shown = rows(folder());
    const int position = (pos.y() + m_scroll.offset) / kRowH;
    if (position < 0 || position >= shown.size())
        return {};
    h.index = shown.at(position);
    h.part = partAt(rowRect(position, m_scroll.offset), pos);
    return h;
}

void LibraryView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    prepare(p);

    if (!m_spotify->isConnected() || m_spotify->library().isEmpty()) {
        paintPlaceholder(p);
        return;
    }

    if (m_detail < 0 || m_detailT < 1.0) {
        p.save();
        p.setOpacity(std::max(0.0, 1.0 - 2.0 * m_detailT));
        if (m_slideT < 1.0) {
            // the new page comes in from the right going in, from the left going back
            const qreal w = lw();
            const qreal dir = m_slideForward ? 1.0 : -1.0;
            paintPage(p, m_slideFrom, m_slideFromOffset, -dir * w * m_slideT, false);
            paintPage(p, folder(), m_scroll.offset, dir * w * (1.0 - m_slideT), false);
        } else {
            paintPage(p, folder(), m_scroll.offset, 0, true);
        }
        p.restore();
    }
    if (m_detail >= 0 || m_slideT < 1.0)
        return;

    const int contentH = int(rows(folder()).size()) * kRowH;
    paintEdgeFades(p, logicalRect(), m_scroll.offset, maxScroll());
    if (m_hover.index >= 0 || m_scroll.dragged)
        paintScrollbar(p, QRectF(logicalRect()).adjusted(0, 0, -1, 0), contentH, m_scroll.offset, maxScroll());
}

void LibraryView::paintPage(QPainter &p, const QString &folder, int offset, qreal dx, bool interactive)
{
    const auto &items = m_spotify->library();
    const QList<int> shown = rows(folder);
    p.save();
    p.setClipRect(logicalRect());
    p.translate(dx, 0);
    const int listH = int(lh());
    const int first = offset / kRowH;
    const int last = std::min(int(shown.size()) - 1, (offset + listH) / kRowH);
    for (int pos = std::max(0, first); pos <= last; ++pos) {
        const int i = shown.at(pos);
        if (i == m_detail)
            continue;
        const LibraryItem &item = items.at(i);
        RowState state;
        state.hovered = interactive && m_hover.index == i && !m_scroll.dragged;
        state.hoverPart = state.hovered ? m_hover.part : RowPart::None;
        state.active = item.kind == LibraryItem::Folder ? contains(item.uri, m_spotify->activeUri())
                                                        : item.uri == m_spotify->activeUri();
        state.playing = m_playing;
        paintRow(p, font(), rowRect(pos, offset), item, m_covers->get(item.imageUrl), state);
    }
    p.restore();
}

void LibraryView::paintPlaceholder(QPainter &p)
{
    QString head;
    QString hint;
    if (!m_spotify->isConnected()) {
        head = tr("Підключи Spotify");
        hint = tr("Натисни, щоб увійти й побачити свою бібліотеку");
    } else if (m_spotify->isLoading()) {
        head = tr("Завантажую бібліотеку…");
    } else {
        head = tr("Бібліотека порожня");
    }
    const QRect area = logicalRect();
    QFont f = font();
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.setPen(m_spotify->isConnected() ? th().text : th().accent);
    p.drawText(area.adjusted(0, 0, 0, -18), Qt::AlignCenter, head);
    f.setPixelSize(11);
    f.setBold(false);
    p.setFont(f);
    p.setPen(th().subText);
    p.drawText(area.adjusted(0, 22, 0, 0), Qt::AlignCenter, hint);
}

void LibraryView::wheelEvent(QWheelEvent *event)
{
    if (m_detail >= 0) {
        event->ignore();
        return;
    }
    m_scroll.wheel(event, kRowH, maxScroll());
    m_hover = hitTest(logical(event->position()));
    update();
    event->accept();
}

void LibraryView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_scroll.press(logical(event->position()).y());
}

void LibraryView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = logical(event->position());
    if (m_detail < 0 && m_slideT >= 1.0 && m_scroll.drag(pos.y(), maxScroll())) {
        update();
        return;
    }

    const Hover hover = hitTest(pos);
    setCursor(hover.index >= 0 || !m_spotify->isConnected() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (hover.index != m_hover.index || hover.part != m_hover.part) {
        const bool folderRow = hover.index >= 0 && m_spotify->library().at(hover.index).kind == LibraryItem::Folder;
        if (hover.part == RowPart::Nfc && !folderRow)
            QToolTip::showText(event->globalPosition().toPoint(), nfcTooltip(), this);
        else if (m_hover.part == RowPart::Nfc)
            QToolTip::hideText();
        m_hover = hover;
        update();
    }
}

void LibraryView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_scroll.pressed)
        return;
    if (m_scroll.release()) {
        update();
        return;
    }
    if (!m_spotify->isConnected()) {
        runSpotifySetup(m_spotify, window());
        return;
    }

    const Hover hit = hitTest(logical(event->position()));
    if (hit.index < 0)
        return;
    const LibraryItem item = m_spotify->library().at(hit.index);
    if (item.kind == LibraryItem::Folder) {
        openFolder(item.uri);
        return;
    }
    switch (hit.part) {
    case RowPart::Art:
        if (item.uri == m_spotify->activeUri())
            emit togglePlayRequested();
        else
            m_spotify->play(item.uri);
        break;
    case RowPart::Nfc:
        emit writeRecordRequested(item);
        break;
    case RowPart::Text:
        openDetail(hit.index);
        break;
    case RowPart::None:
        break;
    }
}

void LibraryView::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = {};
    update();
}
