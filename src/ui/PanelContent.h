#pragma once

#include <QVariantAnimation>
#include <QWidget>

class CoverCache;
class DeviceController;
class ErrorToasts;
class LibraryView;
class MediaController;
class PlayerView;
class PlaylistScrubPopup;
class SettingsPage;
class SpotifyClient;
class TabStrip;
class TrackListPopup;

class PanelContent : public QWidget
{
    Q_OBJECT

public:
    PanelContent(MediaController *media, SpotifyClient *spotify, DeviceController *device, QWidget *parent = nullptr);

    void closeTrackList(bool fadeHeader = false);
    void closeSettings();
    void updatePopupAnchor();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void openSettings();
    void layoutChildren();

    SpotifyClient *m_spotify;
    CoverCache *m_covers;
    PlayerView *m_player;
    LibraryView *m_library;
    TrackListPopup *m_tracks;
    PlaylistScrubPopup *m_scrubPopup;
    TabStrip *m_tabs;
    QWidget *m_settingsHost;
    SettingsPage *m_settings;
    ErrorToasts *m_errors;
    bool m_settingsOpen = false;
    QVariantAnimation m_settingsAnim;
    QString m_trackKey;
};
