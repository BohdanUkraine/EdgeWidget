#pragma once

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QSlider;
class DeviceController;
class SpotifyClient;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    SettingsPage(SpotifyClient *spotify, DeviceController *device, QWidget *parent = nullptr);

signals:
    void backRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void addSizeSection(QGridLayout *grid, int &row);
    void addPositionSection(QGridLayout *grid, int &row);
    void addLookSection(QGridLayout *grid, int &row);
    void addAccountSection(QGridLayout *grid, int &row);
    void addPlayerSection(QGridLayout *grid, int &row);
    void addAboutSection(QGridLayout *grid, int &row);

    QSlider *addSlider(QGridLayout *grid, int &row, const QString &title, int min, int max, int step,
                       const QString &unit, std::function<void(int)> apply);
    QList<QPushButton *> addChoice(QGridLayout *grid, int &row, const QString &title, const QStringList &options);
    void addHint(QGridLayout *grid, int &row, const QString &text);

    void syncFromSettings();
    void syncAccount();
    void syncPlayer();
    void applyStyle();
    void pickCustomColor();

    SpotifyClient *m_spotify;
    DeviceController *m_device;
    QScrollArea *m_scrollArea = nullptr;

    QSlider *m_scale = nullptr;
    QSlider *m_handleWidth = nullptr;
    QSlider *m_handleHeight = nullptr;
    QSlider *m_cardHeight = nullptr;
    QSlider *m_scrubPopupHeight = nullptr;
    QSlider *m_position = nullptr;
    QPushButton *m_left = nullptr;
    QPushButton *m_right = nullptr;
    QCheckBox *m_handleVisible = nullptr;
    QPushButton *m_dark = nullptr;
    QPushButton *m_light = nullptr;
    QList<QPushButton *> m_swatches;
    QPushButton *m_customColor = nullptr;
    QComboBox *m_language = nullptr;
    QComboBox *m_layer = nullptr;
    QCheckBox *m_autostart = nullptr;

    QLabel *m_accountStatus = nullptr;
    QPushButton *m_login = nullptr;
    QPushButton *m_reload = nullptr;
    QPushButton *m_logout = nullptr;
    QLabel *m_playerStatus = nullptr;
    QPushButton *m_faderTimeline = nullptr;
    QPushButton *m_faderPlaylist = nullptr;
    QPushButton *m_volumeSystem = nullptr;
    QPushButton *m_volumeSpotify = nullptr;
    QSlider *m_platterSpeed = nullptr;
    QPushButton *m_platterTest = nullptr;

    bool m_syncing = false;
    QString m_styleKey;
};
