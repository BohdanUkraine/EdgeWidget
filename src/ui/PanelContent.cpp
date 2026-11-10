#include "ui/PanelContent.h"

#include "app/AppSettings.h"
#include "device/DeviceController.h"
#include "spotify/SpotifyLauncher.h"
#include "ui/ErrorToasts.h"
#include "ui/library/LibraryCard.h"
#include "ui/library/LibraryView.h"
#include "playback/MediaController.h"
#include "ui/NowPlayingPopup.h"
#include "ui/PanelLayout.h"
#include "ui/PlayerView.h"
#include "ui/PlaylistScrubPopup.h"
#include "ui/VolumeOverlay.h"
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
}

PanelContent::PanelContent(MediaController *media, SpotifyClient *spotify, DeviceController *device, QWidget *parent)
    : QWidget(parent)
    , m_spotify(spotify)
    , m_covers(new CoverCache(spotify, this))
    , m_player(new PlayerView(media, m_covers, this))
    , m_library(new LibraryView(spotify, m_covers, this))
    , m_tracks(new TrackListPopup(spotify, m_covers, parent ? parent->window() : nullptr))
    , m_card(new LibraryCard(parent ? parent->window() : nullptr))
    , m_scrubPopup(new PlaylistScrubPopup(m_covers, parent ? parent->window() : nullptr))
    , m_nowPlaying(new NowPlayingPopup(spotify, m_covers, parent ? parent->window() : nullptr))
    , m_tabs(new TabStrip(this))
    , m_settingsHost(new QWidget(this))
    , m_settings(new SettingsPage(spotify, device, m_settingsHost))
    , m_errors(new ErrorToasts(this))
{
    setAttribute(Qt::WA_TranslucentBackground);

    connect(spotify, &SpotifyClient::error, m_errors, &ErrorToasts::showError);
    connect(&SpotifyLauncher::instance(), &SpotifyLauncher::failed, m_errors, &ErrorToasts::showError);
    connect(device, &DeviceController::deviceError, m_errors, &ErrorToasts::showError);

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
        if (tab == TabStrip::Filters) {
            const bool open = !m_card->filtersOpen() || m_settingsOpen;
            closeSettings();
            m_card->setFiltersOpen(open);
            m_tabs->setFilters(open, m_library->filter() != LibraryView::Filter::All);
            return;
        }
        if (tab == TabStrip::Settings && !m_settingsOpen)
            openSettings();
        else
            closeSettings();
    });
    connect(&AppSettings::instance(), &AppSettings::changed, this, &PanelContent::layoutChildren);

    m_player->setRecordColor(device->recordColor());
    connect(device, &DeviceController::recordColorChanged, m_player, &PlayerView::setRecordColor);

    connect(device, &DeviceController::playlistScrubMoved, this, [this](const QList<Track> &tracks, int index) {
        if (!m_tracks->openUri().isEmpty() && m_tracks->openUri() == m_spotify->activeUri())
            m_tracks->previewIndex(index);
        else
            m_scrubPopup->showPreview(tracks, index);
        if (index >= 0 && index < tracks.size()) {
            const Track &t = tracks.at(index);
            m_player->setPreview(t.name, t.artists, t.imageUrl, t.durationMs);
        }
    });
    connect(device, &DeviceController::playlistScrubEnded, this, [this] { m_scrubPopup->hidePreview(); });
    connect(device, &DeviceController::recordPlayed, this, [this](const QString &uri, const QColor &color) {
        if (!m_expanded && m_spotify->isConnected())
            m_nowPlaying->showRecord(uri, color);
    });
    auto *volumeOverlay = new VolumeOverlay(parent ? parent->window() : nullptr);
    connect(device, &DeviceController::spotifyVolumeChanged, volumeOverlay, &VolumeOverlay::showLevel);

    auto updateContextName = [this] {
        const auto &lib = m_spotify->library();
        const auto it = std::find_if(lib.begin(), lib.end(),
                                     [this](const LibraryItem &i) { return i.uri == m_spotify->activeUri(); });
        m_player->setContextName(m_spotify->activeUri().isEmpty() || it == lib.end() ? QString() : it->name);
    };
    connect(spotify, &SpotifyClient::activeChanged, this, updateContextName);
    connect(spotify, &SpotifyClient::libraryChanged, this, updateContextName);
    updateContextName();
    connect(m_player, &PlayerView::contextClicked, this, [this] {
        const QString uri = m_spotify->activeUri();
        closeSettings();
        if (m_library->reveal(uri))
            m_tracks->revealNowPlaying(uri);
    });

    connect(m_library, &LibraryView::togglePlayRequested, media, &MediaController::togglePlayPause);
    connect(m_library, &LibraryView::detailOpened, this, [this](const LibraryItem &item, int rowTop) {
        updatePopupAnchor();
        m_tracks->open(item, rowTop);
        m_detailOpen = true;
        updateCard();
    });
    connect(m_library, &LibraryView::detailClosed, m_tracks, &TrackListPopup::close);
    connect(m_library, &LibraryView::detailClosed, this, [this] {
        m_detailOpen = false;
        updateCard();
    });

    connect(m_library, &LibraryView::pageChanged, this, [this] { m_card->setTitle(m_library->folderTitle()); });
    connect(m_library, &LibraryView::filterChanged, this, [this] {
        m_card->setFilter(m_library->filter());
        m_tabs->setFilters(m_card->filtersOpen(), m_library->filter() != LibraryView::Filter::All);
    });
    connect(m_card, &LibraryCard::backClicked, m_library, &LibraryView::goBack);
    connect(m_card, &LibraryCard::filterClicked, this, [this](LibraryView::Filter filter) {
        m_library->setFilter(m_library->filter() == filter ? LibraryView::Filter::All : filter);
    });
    connect(m_tracks, &TrackListPopup::closeRequested, m_library, [this] { m_library->closeDetail(); });
    connect(m_tracks, &TrackListPopup::togglePlayRequested, media, &MediaController::togglePlayPause);

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
        if (state.trackKey() != m_trackKey) {
            m_trackKey = state.trackKey();
            if (state.available && !state.title.isEmpty())
                QTimer::singleShot(700, m_spotify, [this, title = state.title] { m_spotify->updateNowPlaying(title); });
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
    const QRect errors = scaled(kErrorsInset, kErrorsTop, kWidth - kTabsW - 2 * kErrorsInset, 0);
    m_errors->setAnchor(errors.topLeft(), errors.width());

    const int pageW = m_player->width();
    m_settingsHost->setGeometry(0, 0, pageW, height());
    m_settings->setFixedSize(pageW, height());
    if (m_settingsAnim.state() != QAbstractAnimation::Running)
        m_settings->move(m_settingsOpen ? 0 : pageW, 0);
}

void PanelContent::updatePopupAnchor()
{
    const QRect column(m_library->mapToGlobal(QPoint(0, 0)), m_library->size());
    m_tracks->setArea(column, mapToGlobal(QPoint(0, 0)).y(), mapToGlobal(QPoint(0, height())).y());
    m_card->setArea(column, mapToGlobal(QPoint(0, 0)).y(), mapToGlobal(QPoint(0, height())).y());
    m_scrubPopup->setAnchor(QRect(window()->mapToGlobal(QPoint(0, 0)), window()->size()),
                            AppSettings::instance().rightSide(), isVisible());
}

void PanelContent::closeTrackList(bool fadeHeader)
{
    m_library->closeDetail(fadeHeader);
}

void PanelContent::setExpanded(bool expanded)
{
    m_expanded = expanded;
    if (expanded)
        m_nowPlaying->dismiss();
    updateCard();
}

// Folder name and filters stay as they are; the card just steps away while something
// covers its spot or the panel is collapsed.
void PanelContent::updateCard()
{
    m_card->setSuppressed(!m_expanded || m_settingsOpen || m_detailOpen);
}

void PanelContent::openSettings()
{
    closeTrackList(true);
    m_settingsOpen = true;
    updateCard();
    m_tabs->setCurrent(TabStrip::Settings);
    layoutChildren();
    m_settings->move(m_settingsHost->width(), 0);
    m_settingsHost->show();
    m_settingsHost->raise();
    m_errors->raise();
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
    updateCard();
    m_tabs->setCurrent(TabStrip::Library);
    m_settingsAnim.stop();
    m_settingsAnim.setStartValue(1.0 - qreal(m_settings->x()) / std::max(1, m_settingsHost->width()));
    m_settingsAnim.setEndValue(0.0);
    m_settingsAnim.start();
}
