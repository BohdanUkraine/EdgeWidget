#pragma once

#include <QVariantAnimation>
#include <QWidget>

class CoverCache;
class DeviceController;
class ErrorToasts;
class LibraryCard;
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
    void setExpanded(bool expanded); // the library card shows only on an expanded panel

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void openSettings();
    void layoutChildren();
    void updateCard();

    SpotifyClient *m_spotify;
    CoverCache *m_covers;
    PlayerView *m_player;
    LibraryView *m_library;
    TrackListPopup *m_tracks;
    LibraryCard *m_card;
    PlaylistScrubPopup *m_scrubPopup;
    TabStrip *m_tabs;
    QWidget *m_settingsHost;
    SettingsPage *m_settings;
    ErrorToasts *m_errors;
    bool m_settingsOpen = false;
    bool m_expanded = false;
    bool m_detailOpen = false;
    QVariantAnimation m_settingsAnim;
    QString m_trackKey;
};
