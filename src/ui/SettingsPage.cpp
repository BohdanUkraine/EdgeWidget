#include "ui/SettingsPage.h"

#include "app/AppSettings.h"
#include "platform/Autostart.h"
#include "app/Localization.h"
#include "device/DeviceController.h"
#include "spotify/SpotifyClient.h"
#include "ui/SpotifySetup.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kBorder = 2; // keep the panel's own top / bottom border visible

const QList<QColor> kSwatches = {
    QColor("#1ED760"), QColor("#3D9BFF"), QColor("#A970FF"), QColor("#F48FB1"),
    QColor("#FF9F43"), QColor("#FF5C5C"), QColor("#FFD43B"), QColor("#BDBDBD"),
};

// Stylesheets can only draw images, so the combo box arrow and the check mark are tiny PNGs
// in the cache dir (drawn at 2x for HiDPI).
QString strokeImage(const char *name, const QSize &size, const QPolygonF &line, const QColor &color)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/%1-%2.png").arg(QLatin1String(name), color.name().mid(1));
    if (!QFileInfo::exists(path)) {
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(line);
        p.end();
        img.save(path);
    }
    return path;
}

QString arrowImage(const QColor &color)
{
    return strokeImage("combo-arrow", { 20, 12 }, QPolygonF({ { 3, 3 }, { 10, 9 }, { 17, 3 } }), color);
}

QString checkImage(const QColor &color)
{
    return strokeImage("check", { 20, 20 }, QPolygonF({ { 4, 10.5 }, { 8.5, 15 }, { 16, 5.5 } }), color);
}

QLabel *label(const QString &text, const char *objectName = nullptr)
{
    auto *l = new QLabel(text);
    if (objectName)
        l->setObjectName(objectName);
    return l;
}

QPushButton *button(const QString &text)
{
    auto *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
} // namespace

SettingsPage::SettingsPage(SpotifyClient *spotify, DeviceController *device, QWidget *parent)
    : QWidget(parent)
    , m_spotify(spotify)
    , m_device(device)
{
    connect(m_device, &DeviceController::connectedChanged, this, &SettingsPage::syncPlayer);
    connect(m_device, &DeviceController::recordColorChanged, this, &SettingsPage::syncPlayer);
    connect(&AppSettings::instance(), &AppSettings::changed, this, &SettingsPage::syncFromSettings);
    connect(m_spotify, &SpotifyClient::connectedChanged, this, &SettingsPage::syncAccount);
    connect(m_spotify, &SpotifyClient::libraryChanged, this, &SettingsPage::syncAccount);
    // Queued: the language list itself gets rebuilt, never delete it inside its own signal.
    connect(&Localization::instance(), &Localization::languageChanged, this, &SettingsPage::buildUi,
            Qt::QueuedConnection);
    buildUi();
}

void SettingsPage::buildUi()
{
    const QObjectList old = children(); // rebuilt from scratch on language change
    for (QObject *o : old)
        delete o;
    m_swatches.clear();
    m_styleKey.clear();

    auto *back = new QToolButton;
    back->setObjectName("back");
    back->setText(QStringLiteral("‹"));
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QToolButton::clicked, this, &SettingsPage::backRequested);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(4, 0, 10, 0);
    header->addWidget(back);
    header->addWidget(label(tr("Налаштування"), "title"));
    header->addStretch();

    auto *body = new QWidget;
    auto *grid = new QGridLayout(body);
    grid->setContentsMargins(14, 0, 18, 12);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    int row = 0;
    addSizeSection(grid, row);
    addPositionSection(grid, row);
    addLookSection(grid, row);
    addAccountSection(grid, row);
    addPlayerSection(grid, row);
    addAboutSection(grid, row);

    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidget(body);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, kBorder + 4, 0, kBorder);
    layout->setSpacing(2);
    layout->addLayout(header);
    layout->addWidget(m_scrollArea, 1);

    syncFromSettings();
    syncAccount();
    syncPlayer();
}

// ---------------------------------------------------------------- sections

void SettingsPage::addSizeSection(QGridLayout *grid, int &row)
{
    AppSettings &s = AppSettings::instance();
    grid->addWidget(label(tr("Розміри"), "section"), row++, 0, 1, 3);
    m_scale = addSlider(grid, row, tr("Панель"), 70, 160, 5, "%", [&s](int v) { s.setScale(v / 100.0); });
    m_handleWidth = addSlider(grid, row, tr("Кнопка: ширина"), 12, 48, 1, " px", [&s](int v) { s.setHandleWidth(v); });
    m_handleHeight = addSlider(grid, row, tr("Кнопка: висота"), 40, 400, 5, " px", [&s](int v) { s.setHandleHeight(v); });
    m_cardHeight = addSlider(grid, row, tr("Список треків"), 100, 480, 10, " px", [&s](int v) { s.setCardHeight(v); });
}

void SettingsPage::addPositionSection(QGridLayout *grid, int &row)
{
    grid->addWidget(label(tr("Розташування"), "section"), row++, 0, 1, 3);
    const auto sides = addChoice(grid, row, tr("Край екрана"), { tr("Зліва"), tr("Справа") });
    m_left = sides[0];
    m_right = sides[1];
    connect(m_right, &QPushButton::toggled, this, [this](bool on) {
        if (!m_syncing)
            AppSettings::instance().setRightSide(on);
    });
    m_position = addSlider(grid, row, tr("Висота на екрані"), 0, 100, 1, "%",
                           [](int v) { AppSettings::instance().setPosition(v / 100.0); });
    addHint(grid, row, tr("0 % — угорі, 100 % — унизу. Якщо над панеллю мало місця, треки відкриваються вниз."));

    m_layer = new QComboBox;
    m_layer->setCursor(Qt::PointingHandCursor);
    m_layer->installEventFilter(this);
    m_layer->addItems({ tr("Під вікнами"), tr("Поверх вікон"), tr("Поверх усього") }); // AppSettings::Layer order
    connect(m_layer, &QComboBox::activated, this, [](int index) {
        AppSettings::instance().setLayer(AppSettings::Layer(index));
    });
    auto *layerRow = new QHBoxLayout;
    layerRow->addWidget(m_layer);
    layerRow->addStretch();
    grid->addWidget(label(tr("Поведінка")), row, 0);
    grid->addLayout(layerRow, row++, 1, 1, 2);
    addHint(grid, row, tr("Поверх вікон: повноекранні програми й відео перекривають панель."));

#ifdef Q_OS_MACOS
    m_autostart = new QCheckBox(tr("Запускати при вході в систему"));
#else
    m_autostart = new QCheckBox(tr("Запускати разом з Windows"));
#endif
    m_autostart->setCursor(Qt::PointingHandCursor);
    m_autostart->setChecked(Autostart::isEnabled()); // the registry is the source of truth
    connect(m_autostart, &QCheckBox::toggled, this, [](bool on) { Autostart::setEnabled(on); });
    grid->addWidget(m_autostart, row++, 0, 1, 3);
}

void SettingsPage::addLookSection(QGridLayout *grid, int &row)
{
    grid->addWidget(label(tr("Вигляд"), "section"), row++, 0, 1, 3);
    const auto themes = addChoice(grid, row, tr("Тема"), { tr("Темна"), tr("Світла") });
    m_dark = themes[0];
    m_light = themes[1];
    connect(m_dark, &QPushButton::toggled, this, [this](bool on) {
        if (!m_syncing)
            AppSettings::instance().setDarkTheme(on);
    });

    auto *swatchRow = new QHBoxLayout;
    swatchRow->setSpacing(6);
    for (const QColor &c : kSwatches) {
        auto *b = button({});
        b->setObjectName("swatch");
        b->setFixedSize(20, 20);
        b->setCheckable(true);
        b->setToolTip(c.name());
        b->setProperty("swatchColor", c);
        connect(b, &QPushButton::clicked, this, [c] { AppSettings::instance().setAccent(c); });
        m_swatches.append(b);
        swatchRow->addWidget(b);
    }
    m_customColor = button(QStringLiteral("…"));
    m_customColor->setObjectName("swatch");
    m_customColor->setFixedSize(20, 20);
    m_customColor->setCheckable(true);
    m_customColor->setToolTip(tr("Свій колір"));
    connect(m_customColor, &QPushButton::clicked, this, &SettingsPage::pickCustomColor);
    swatchRow->addWidget(m_customColor);
    swatchRow->addStretch();
    grid->addWidget(label(tr("Колір рамки")), row, 0);
    grid->addLayout(swatchRow, row++, 1, 1, 2);

    // A list, so a new language only needs a dictionary in Localization.cpp.
    m_language = new QComboBox;
    m_language->setCursor(Qt::PointingHandCursor);
    m_language->installEventFilter(this); // the wheel scrolls the page, not the language
    m_language->addItem(tr("Авто"), QStringLiteral("auto"));
    for (const auto &[code, name] : Localization::languages())
        m_language->addItem(name, code);
    connect(m_language, &QComboBox::activated, this, [this](int index) {
        AppSettings::instance().setLanguage(m_language->itemData(index).toString());
    });
    auto *languageRow = new QHBoxLayout;
    languageRow->addWidget(m_language);
    languageRow->addStretch();
    grid->addWidget(label(tr("Мова")), row, 0);
    grid->addLayout(languageRow, row++, 1, 1, 2);
    addHint(grid, row, tr("Авто: за країною акаунта Spotify, інакше мова Windows."));
}

void SettingsPage::addAccountSection(QGridLayout *grid, int &row)
{
    grid->addWidget(label(tr("Spotify"), "section"), row++, 0, 1, 3);
    m_accountStatus = label({}, "hint");
    m_accountStatus->setWordWrap(true);
    grid->addWidget(m_accountStatus, row++, 0, 1, 3);

    m_login = button({});
    m_reload = button(tr("Оновити бібліотеку"));
    m_logout = button(tr("Вийти з акаунта"));
    auto *accountRow = new QHBoxLayout;
    for (QPushButton *b : { m_login, m_reload, m_logout })
        accountRow->addWidget(b);
    accountRow->addStretch();
    grid->addLayout(accountRow, row++, 0, 1, 3);
    // Dialog owned by the panel: the panel stays open while it is up.
    connect(m_login, &QPushButton::clicked, this, [this] { runSpotifySetup(m_spotify, window()); });
    connect(m_reload, &QPushButton::clicked, m_spotify, &SpotifyClient::refreshLibrary);
    connect(m_logout, &QPushButton::clicked, m_spotify, &SpotifyClient::logout);
}

void SettingsPage::addPlayerSection(QGridLayout *grid, int &row)
{
    grid->addWidget(label(tr("Програвач"), "section"), row++, 0, 1, 3);
    m_playerStatus = label({}, "hint");
    m_playerStatus->setWordWrap(true);
    grid->addWidget(m_playerStatus, row++, 0, 1, 3);
}

void SettingsPage::addAboutSection(QGridLayout *grid, int &row)
{
    grid->addWidget(label(tr("Про програму"), "section"), row++, 0, 1, 3);
    // Required by the LGPL: tell users the app uses Qt and under which licence.
    auto *aboutQt = button(tr("Про Qt…"));
    connect(aboutQt, &QPushButton::clicked, this, [this] { QMessageBox::aboutQt(window()); });
    grid->addWidget(aboutQt, row++, 0, 1, 2, Qt::AlignLeft);

    auto *reset = button(tr("Скинути налаштування"));
    connect(reset, &QPushButton::clicked, this, [] { AppSettings::instance().resetToDefaults(); });
    grid->addWidget(reset, row++, 0, 1, 2, Qt::AlignLeft);
}

// ---------------------------------------------------------------- row helpers

QSlider *SettingsPage::addSlider(QGridLayout *grid, int &row, const QString &title, int min, int max, int step,
                                 const QString &unit, std::function<void(int)> apply)
{
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setSingleStep(step);
    slider->setPageStep(step * 4);
    slider->setCursor(Qt::PointingHandCursor);
    slider->installEventFilter(this); // the wheel scrolls the page, not the value
    auto *value = label({}, "value");
    value->setMinimumWidth(44);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(slider, &QSlider::valueChanged, this, [this, slider, value, unit, step, apply](int v) {
        // Snap to the step so values stay round while dragging (re-enters with the snapped value).
        const int snapped = int(std::lround(double(v) / step)) * step;
        if (snapped != v) {
            slider->setValue(snapped);
            return;
        }
        value->setText(QString::number(v) + unit);
        if (!m_syncing)
            apply(v);
    });
    grid->addWidget(label(title), row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(value, row++, 2);
    return slider;
}

// Exclusive toggle buttons in one row (e.g. Left / Right).
QList<QPushButton *> SettingsPage::addChoice(QGridLayout *grid, int &row, const QString &title,
                                             const QStringList &options)
{
    auto *group = new QButtonGroup(this);
    auto *layout = new QHBoxLayout;
    QList<QPushButton *> buttons;
    for (const QString &option : options) {
        auto *b = button(option);
        b->setCheckable(true);
        group->addButton(b);
        layout->addWidget(b);
        buttons.append(b);
    }
    layout->addStretch();
    grid->addWidget(label(title), row, 0);
    grid->addLayout(layout, row++, 1, 1, 2);
    return buttons;
}

void SettingsPage::addHint(QGridLayout *grid, int &row, const QString &text)
{
    auto *hint = label(text, "hint");
    hint->setWordWrap(true);
    grid->addWidget(hint, row++, 0, 1, 3);
}

// ---------------------------------------------------------------- state

void SettingsPage::syncFromSettings()
{
    const AppSettings &s = AppSettings::instance();
    m_syncing = true;
    m_scale->setValue(int(std::lround(s.scale() * 100)));
    m_handleWidth->setValue(s.handleWidth());
    m_handleHeight->setValue(s.handleHeight());
    m_cardHeight->setValue(s.cardHeight());
    m_position->setValue(int(std::lround(s.position() * 100)));
    (s.rightSide() ? m_right : m_left)->setChecked(true);
    (s.darkTheme() ? m_dark : m_light)->setChecked(true);
    bool preset = false;
    for (QPushButton *b : m_swatches) {
        const bool on = b->property("swatchColor").value<QColor>() == s.accent();
        b->setChecked(on);
        preset |= on;
    }
    m_customColor->setChecked(!preset);
    m_language->setCurrentIndex(std::max(0, m_language->findData(s.language())));
    m_layer->setCurrentIndex(int(s.layer()));
    m_syncing = false;
    applyStyle();
}

void SettingsPage::syncAccount()
{
    const bool connected = m_spotify->isConnected();
    if (!connected)
        m_accountStatus->setText(tr("Не підключено. Увійди, щоб бачити свою бібліотеку."));
    else if (!m_spotify->userName().isEmpty())
        m_accountStatus->setText(tr("Підключено: %1").arg(m_spotify->userName()));
    else
        m_accountStatus->setText(tr("Підключено"));
    m_login->setText(connected ? tr("Змінити акаунт / Client ID…") : tr("Увійти в Spotify…"));
    m_reload->setVisible(connected);
    m_logout->setVisible(connected);
}

void SettingsPage::syncPlayer()
{
    if (!m_device->isConnected())
        m_playerStatus->setText(tr("Не підключено. Шукаю програвач (емулятор на цьому ПК)…"));
    else if (m_device->hasRecord())
        m_playerStatus->setText(tr("Підключено. На рідері платівка %1").arg(m_device->recordColor().name().toUpper()));
    else
        m_playerStatus->setText(tr("Підключено. Поклади платівку на рідер"));
}

void SettingsPage::pickCustomColor()
{
    // Parented to the panel window, so the panel does not hide while the dialog is open.
    const QColor c = QColorDialog::getColor(AppSettings::instance().accent(), window(), tr("Колір рамки"));
    if (c.isValid())
        AppSettings::instance().setAccent(c);
    else
        syncFromSettings(); // restore the checked state
}

// The stylesheet depends on the theme only; rebuilt when the theme or accent changes.
void SettingsPage::applyStyle()
{
    const Theme t = Theme::current();
    const QString key = QStringLiteral("%1-%2").arg(t.dark).arg(t.accent.name());
    if (key == m_styleKey)
        return;
    m_styleKey = key;

    const QColor onAccent = t.accent.lightness() > 150 ? QColor(0, 0, 0) : QColor(255, 255, 255);
    QColor selection = t.accent;
    selection.setAlpha(60);
    setStyleSheet(QStringLiteral(
        "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"
        "QLabel { color: %1; font-size: 12px; }"
        "QLabel#title { font-size: 14px; font-weight: 600; }"
        "QLabel#section { color: %2; font-size: 12px; font-weight: 600; padding-top: 8px; }"
        "QLabel#hint, QLabel#value { color: %3; font-size: 11px; }"
        "QToolButton#back { color: %1; background: transparent; border: none; font-size: 22px; padding: 0 6px 2px 6px; }"
        "QToolButton#back:hover { color: %2; }"
        "QSlider::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: %2; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; background: %1; }"
        "QPushButton { color: %1; background: %5; border: 1px solid %6; border-radius: 10px;"
        "  padding: 3px 10px; font-size: 11px; }"
        "QPushButton:hover { border-color: %2; }"
        "QPushButton:checked { background: %2; color: %7; border-color: %2; }"
        "QPushButton#swatch { padding: 0; border-radius: 10px; border: 2px solid transparent; }"
        "QPushButton#swatch:checked { border-color: %1; }"
        "QComboBox { color: %1; background: %5; border: 1px solid %6; border-radius: 10px;"
        "  padding: 3px 26px 3px 10px; font-size: 11px; min-width: 110px; }"
        "QComboBox:hover, QComboBox:on { border-color: %2; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox::down-arrow { image: url(%10); width: 10px; height: 6px; }"
        "QComboBox QAbstractItemView { background: %8; color: %1; border: 1px solid %2; outline: 0;"
        "  selection-background-color: %9; selection-color: %1; padding: 2px; }"
        "QCheckBox { color: %1; font-size: 12px; spacing: 8px; padding-top: 4px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid %6; border-radius: 4px; background: %5; }"
        "QCheckBox::indicator:hover { border-color: %2; }"
        "QCheckBox::indicator:checked { background: %2; border-color: %2; image: url(%11); }"
        "QScrollBar:vertical { width: 4px; background: transparent; margin: 0; }"
        "QScrollBar::handle:vertical { background: %6; border-radius: 2px; min-height: 24px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: none; }")
                      .arg(Theme::css(t.text), Theme::css(t.accent), Theme::css(t.subText), Theme::css(t.track),
                           Theme::css(t.hover), Theme::css(t.line), Theme::css(onAccent), Theme::css(t.bg),
                           Theme::css(selection), arrowImage(t.subText))
                      .arg(checkImage(onAccent)));

    for (QPushButton *b : m_swatches) {
        const QColor c = b->property("swatchColor").value<QColor>();
        b->setStyleSheet(QStringLiteral("QPushButton#swatch { background: %1; }").arg(c.name()));
    }
    m_customColor->setStyleSheet(QStringLiteral("QPushButton#swatch { background: %1; color: %2; }")
                                     .arg(Theme::css(t.hover), Theme::css(t.text)));
}

bool SettingsPage::eventFilter(QObject *watched, QEvent *event)
{
    // Scrolling the page over a slider / list must not change the setting under the cursor.
    const bool control = qobject_cast<QSlider *>(watched) || qobject_cast<QComboBox *>(watched);
    if (event->type() == QEvent::Wheel && control && m_scrollArea) {
        QCoreApplication::sendEvent(m_scrollArea->verticalScrollBar(), event);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void SettingsPage::paintEvent(QPaintEvent *)
{
    // Opaque over the player, but leaves the panel's top / bottom border visible.
    QPainter p(this);
    p.fillRect(rect().adjusted(0, kBorder, 0, -kBorder), Theme::current().bg);
}
