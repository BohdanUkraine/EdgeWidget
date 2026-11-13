#pragma once

#include "ui/library/LibraryStyle.h"
#include "ui/ScaledWidget.h"

#include <QVariantAnimation>

// The library list. Folders open as pages sliding in from the right; one stays open until
// LibraryCard's arrow takes it back (collapsing the panel keeps it). An artist opens the same
// way, as a page of their albums from the API. A filter narrows the list to one kind.
class LibraryView : public ScaledWidget
{
    Q_OBJECT

public:
    enum class Filter { All, Playlists, Albums, Artists };

    LibraryView(SpotifyClient *spotify, CoverCache *covers, QWidget *parent = nullptr);

    void setPlaying(bool playing);
    void closeDetail(bool fadeHeader = false);
    // Goes to the item's folder, scrolls to it, then opens it; false when it is not in the library.
    bool reveal(const QString &uri);

    QString folderTitle() const; // empty at the top level
    void goBack();
    Filter filter() const { return m_filter; }
    void setFilter(Filter filter);

signals:
    void togglePlayRequested();
    void detailOpened(const LibraryItem &item, int rowTopGlobal);
    void detailClosed(bool fadeHeader);
    void writeRecordRequested(const LibraryItem &item);
    void pageChanged(); // another folder, or its name changed
    void filterChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Hover
    {
        int row = -1; // on the open page
        LibraryStyle::RowPart part = LibraryStyle::RowPart::None;
    };

    enum class Albums { Loading, Failed, Ready };

    QString folder() const { return m_path.isEmpty() ? QString() : m_path.last(); }
    static bool isArtist(const QString &page) { return page.startsWith(QLatin1String("spotify:artist:")); }
    QList<LibraryItem> items(const QString &page) const; // shown on that page, in order
    int rowOf(const QString &page, const QString &uri) const;
    bool passes(const LibraryItem &item) const;   // through the filter
    bool contains(const QString &folder, const QString &uri) const; // anywhere below it
    QStringList pathTo(const QString &folder) const;
    QRect rowRect(int position, int offset) const;
    int maxScroll() const;
    Hover hitTest(const QPoint &pos) const;
    void openDetail(const LibraryItem &item, int row);
    void openFolder(const QString &uri);
    void openArtist(const QString &uri);
    void activate(const LibraryItem &item, int row); // what a click on its name does
    void slide(const QString &fromFolder, int fromOffset, bool forward);
    void paintPage(QPainter &p, const QString &folder, int offset, qreal dx, bool interactive);
    void paintPlaceholder(QPainter &p);

    SpotifyClient *m_spotify;
    CoverCache *m_covers;
    bool m_playing = false;
    LibraryStyle::ScrollGesture m_scroll;
    Hover m_hover;
    Filter m_filter = Filter::All;

    QStringList m_path;       // open folders, outermost first; empty: the top level
    QList<int> m_pathOffsets; // scroll of each page under the open one
    QVariantAnimation m_slideAnim;
    qreal m_slideT = 1.0;
    QString m_slideFrom;
    int m_slideFromOffset = 0;
    bool m_slideForward = true;

    bool m_detail = false; // shown, or still fading out
    bool m_detailOpen = false;
    qreal m_detailT = 0.0;
    QVariantAnimation m_detailAnim;
    QString m_detailUri;

    QVariantAnimation m_scrollAnim;
    QString m_revealUri; // opens when the scroll gets there

    QHash<QString, QList<LibraryItem>> m_artistAlbums;
    QHash<QString, Albums> m_artistState;
};
