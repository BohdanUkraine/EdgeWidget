#include "ui/PanelContent.h"

#include "app/AppSettings.h"
#include "device/DeviceController.h"
#include "ui/library/LibraryView.h"
#include "playback/MediaController.h"
#include "ui/PanelLayout.h"
#include "ui/PlayerView.h"
#include "ui/SettingsPage.h"
#include "ui/TabStrip.h"
#include "ui/library/TrackListPopup.h"

#include <QCursor>
#include <QTimer>
#include <QToolTip>

#include <algorithm>
#include <cmath>

using namespace PanelLayout;

namespace {
constexpr int kSettingsAnimMs = 260;
} // namespace

PanelContent::PanelContent(MediaController *media, SpotifyClient *spotify, DeviceController *device, QWidget *parent)
    : QWidget(parent)
    , m_spotify(spotify)
    , m_covers(new CoverCache(spotify, this))
    , m_player(new PlayerView(media, this))
    , m_library(new LibraryView(spotify, m_covers, this))
    , m_tracks(new TrackListPopup(spotify, m_covers, parent ? parent->window() : nullptr))
    , m_tabs(new TabStrip(this))
    , m_settingsHost(new QWidget(this))
    , m_settings(new SettingsPage(spotify, device, m_settingsHost))
{
    setAttribute(Qt::WA_TranslucentBackground);

    // Settings page: slides in from the right, like a phone settings screen.
    m_settingsHost->hide();
    m_settingsAnim.setDuration(kSettingsAnimMs);
    m_settingsAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_settingsAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_settings->move(int(std::lround(m_settingsHost->width() * (1.0 - v.toReal()))), 0);
    });
    connect(&m_settingsAnim, &QVariantAnimation::finished, this, [this] {
        if (!m_settingsOpen)
            m_settingsHost->hide();
    });
    connect(m_settings, &SettingsPage::backRequested, this, &PanelContent::closeSettings);
    connect(m_tabs, &TabStrip::tabClicked, this, [this](TabStrip::Tab tab) {
        if (tab == TabStrip::Settings && !m_settingsOpen)
            openSettings();
        else
            closeSettings();
    });
    connect(&AppSettings::instance(), &AppSettings::changed, this, &PanelContent::layoutChildren);

    m_player->setRecordColor(device->recordColor());
    connect(device, &DeviceController::recordColorChanged, m_player, &PlayerView::setRecordColor);

    // Library list <-> track list card.
    connect(m_library, &LibraryView::togglePlayRequested, media, &MediaController::togglePlayPause);
    connect(m_library, &LibraryView::detailOpened, this, [this](const LibraryItem &item, int rowTop) {
        updatePopupAnchor();
        m_tracks->open(item, rowTop);
    });
    connect(m_library, &LibraryView::detailClosed, m_tracks, &TrackListPopup::close);
    connect(m_tracks, &TrackListPopup::closeRequested, m_library, [this] { m_library->closeDetail(); });
    connect(m_tracks, &TrackListPopup::togglePlayRequested, media, &MediaController::togglePlayPause);

    // NFC button: writes this album / playlist to the record lying on the player.
    auto writeRecord = [device](const LibraryItem &item) {
        if (!device->isConnected())
            QToolTip::showText(QCursor::pos(), tr("Програвач не підключено"));
        else if (!device->writeRecord(item.uri))
            QToolTip::showText(QCursor::pos(), tr("Спершу поклади платівку на програвач"));
        else
            QToolTip::showText(QCursor::pos(), tr("Записую «%1» на платівку").arg(item.name));
    };
    connect(m_library, &LibraryView::writeRecordRequested, this, writeRecord);
    connect(m_tracks, &TrackListPopup::writeRecordRequested, this, writeRecord);

    connect(media, &MediaController::stateChanged, this, [this](const MediaState &state) {
        m_library->setPlaying(state.available && state.playing);
        m_tracks->setNowPlaying(state.available ? state.title : QString(), state.available && state.playing);
        // New track: ask Spotify which playlist / album it belongs to, to highlight it.
        if (state.trackKey() != m_trackKey) {
            m_trackKey = state.trackKey();
            if (state.available && !state.title.isEmpty())
                QTimer::singleShot(700, m_spotify, &SpotifyClient::updateNowPlaying);
        }
    });
}

void PanelContent::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutChildren();
}

void PanelContent::layoutChildren()
{
    const qreal s = uiScale();
    auto scaled = [s](qreal x, qreal y, qreal w, qreal h) { return QRectF(x * s, y * s, w * s, h * s).toRect(); };

    m_player->setGeometry(scaled(0, 0, kWidth - kTabsW, kHeight));
    m_library->setGeometry(scaled(kRightX, kLibraryTop, kRightEnd - kRightX, kLibraryH));
    m_tabs->setGeometry(scaled(kWidth - kTabsW, 0, kTabsW, kHeight));

    const int pageW = m_player->width();
    m_settingsHost->setGeometry(0, 0, pageW, height());
    m_settings->setFixedSize(pageW, height());
    if (m_settingsAnim.state() != QAbstractAnimation::Running)
        m_settings->move(m_settingsOpen ? 0 : pageW, 0);
}

// The opened album covers the library column and grows out of the panel's edge.
void PanelContent::updatePopupAnchor()
{
    const QRect column(m_library->mapToGlobal(QPoint(0, 0)), m_library->size());
    m_tracks->setArea(column, mapToGlobal(QPoint(0, 0)).y(), mapToGlobal(QPoint(0, height())).y());
}

void PanelContent::closeTrackList(bool fadeHeader)
{
    m_library->closeDetail(fadeHeader); // also closes the card
}

void PanelContent::openSettings()
{
    closeTrackList(true); // the album title fades with the card instead of flying back under the page
    m_settingsOpen = true;
    m_tabs->setCurrent(TabStrip::Settings);
    layoutChildren();
    m_settings->move(m_settingsHost->width(), 0);
    m_settingsHost->show();
    m_settingsHost->raise();
    m_settingsAnim.stop();
    m_settingsAnim.setStartValue(0.0);
    m_settingsAnim.setEndValue(1.0);
    m_settingsAnim.start();
}

void PanelContent::closeSettings()
{
    if (!m_settingsOpen)
        return;
    m_settingsOpen = false;
    m_tabs->setCurrent(TabStrip::Library);
    m_settingsAnim.stop();
    m_settingsAnim.setStartValue(1.0 - qreal(m_settings->x()) / std::max(1, m_settingsHost->width()));
    m_settingsAnim.setEndValue(0.0);
    m_settingsAnim.start();
}
