#pragma once

#include "app/AppSettings.h"
#include "spotify/SpotifyClient.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>

class QPainter;
class QWheelEvent;

// Look shared by the library list and the track list popup (Spotify "Your Library" style).
namespace LibraryStyle {

constexpr int kRowH = 44;
constexpr int kArt = 40;
constexpr int kNfcSize = 28;
constexpr int kTrackH = 40;

inline Theme th() { return Theme::current(); }

enum class RowPart { None, Art, Text, Nfc };

QRectF artRect(const QRect &row);
QRect nfcRect(const QRect &row);
RowPart partAt(const QRect &row, const QPoint &pos);

struct RowState
{
    bool hovered = false;
    RowPart hoverPart = RowPart::None;
    bool active = false;  // this item is the current Spotify context
    bool playing = false; // and music is playing
};

void paintRow(QPainter &p, const QFont &base, const QRect &row, const LibraryItem &item, const QImage &cover,
              const RowState &state);
void paintTrack(QPainter &p, const QFont &base, const QRect &row, const QImage &cover, const Track &track,
                bool hovered, bool current, bool playing);
void paintScrollbar(QPainter &p, const QRectF &track, int contentH, int scroll, int maxScroll);
// Fades the top / bottom of a scrolled list into the background (only where there is more).
void paintEdgeFades(QPainter &p, const QRectF &area, int scroll, int maxScroll);
QString formatTime(qint64 ms);

QString nfcTooltip(); // the NFC record button

// Vertical scrolling of a list by wheel or by dragging; a press that did not drag is a click.
struct ScrollGesture
{
    int offset = 0;
    bool pressed = false;
    bool dragged = false;

    void press(int y);
    bool drag(int y, int maxOffset); // true while dragging (offset updated)
    bool release();                  // true if the press turned into a drag (not a click)
    void wheel(const QWheelEvent *event, int step, int maxOffset);
    void clamp(int maxOffset);

private:
    int m_pressY = 0;
    int m_pressOffset = 0;
};

} // namespace LibraryStyle

// Album / playlist covers by URL, downloaded once and shared by every view.
class CoverCache : public QObject
{
    Q_OBJECT

public:
    explicit CoverCache(SpotifyClient *spotify, QObject *parent = nullptr);

    // Null image until downloaded; coverReady() fires when it arrives.
    QImage get(const QString &url);

signals:
    void coverReady();

private:
    SpotifyClient *m_spotify;
    QHash<QString, QImage> m_covers;
    QSet<QString> m_requested;
};
