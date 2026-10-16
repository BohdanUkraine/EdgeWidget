#pragma once

#include <QVariantAnimation>
#include <QWidget>

class CoverCache;
class DeviceController;
class LibraryView;
class MediaController;
class PlayerView;
class SettingsPage;
class SpotifyClient;
class TabStrip;
class TrackListPopup;

// Everything inside the edge panel: lays out the player, the library list, the tab strip
// and the settings page (which slides over the player), and wires them together.
class PanelContent : public QWidget
{
    Q_OBJECT

public:
    PanelContent(MediaController *media, SpotifyClient *spotify, DeviceController *device, QWidget *parent = nullptr);

    void closeTrackList(bool fadeHeader = false);
    void closeSettings();
    void updatePopupAnchor(); // call whenever the panel moves

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void openSettings();
    void layoutChildren();

    SpotifyClient *m_spotify;
    CoverCache *m_covers;
    PlayerView *m_player;
    LibraryView *m_library;
    TrackListPopup *m_tracks; // own window, owned by the panel
    TabStrip *m_tabs;
    QWidget *m_settingsHost;  // clips the sliding page so it never covers the tabs
    SettingsPage *m_settings;
    bool m_settingsOpen = false;
    QVariantAnimation m_settingsAnim;
    QString m_trackKey;
};
