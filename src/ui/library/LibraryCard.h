#pragma once

#include "ui/library/LibraryView.h"
#include "ui/ScaledWidget.h"

#include <QVariantAnimation>

// Card that comes out from behind the panel's frame above the library: the open folder's
// name with a back arrow, and under it the filters when they are open. Each row slides out
// from behind the one nearer the panel (the filters from behind the frame, the name from
// behind the filters) and the card's edge follows. Gone when neither is there.
class LibraryCard : public ScaledWidget
{
    Q_OBJECT

public:
    explicit LibraryCard(QWidget *owner);

    void setArea(const QRect &column, int panelTop, int panelBottom);
    void setTitle(const QString &title); // empty: no folder open
    void setFiltersOpen(bool open);
    bool filtersOpen() const { return m_filtersOpen; }
    void setFilter(LibraryView::Filter filter);
    // The playlist card or the settings are over it, or the panel is collapsed.
    void setSuppressed(bool suppressed);

signals:
    void backClicked();
    void filterClicked(LibraryView::Filter filter);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class Part { None, Back, Playlists, Albums, Artists };

    // Row positions (logical, window coordinates) for how far each row is out right now.
    qreal filtersY() const;
    qreal titleY() const;
    qreal cardEdge() const; // the card's outer edge: its top, or its bottom when below the panel
    QRect titleRect() const;
    QRect chipRect(int i) const;
    Part partAt(const QPoint &pos) const;
    void applyGeometry();
    void animate();
    void animateRow(QVariantAnimation &anim, qreal from, bool out);

    QString m_title;      // as set
    QString m_shownTitle; // stays while the name slides back in
    bool m_filtersOpen = false;
    LibraryView::Filter m_filter = LibraryView::Filter::All;
    bool m_suppressed = true;

    QRect m_column;
    int m_panelTop = 0;
    int m_panelBottom = 0;
    bool m_down = false; // no room above the panel: below it instead

    qreal m_titleOut = 0.0;   // 0 hidden .. 1 fully out
    qreal m_filtersOut = 0.0;
    QVariantAnimation m_titleAnim;
    QVariantAnimation m_filtersAnim;
    Part m_hover = Part::None;
};
