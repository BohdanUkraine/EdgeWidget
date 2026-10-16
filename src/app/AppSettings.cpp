#include "app/AppSettings.h"

#include <QSettings>

#include <algorithm>

namespace {
constexpr qreal kDefaultScale = 1.0;
constexpr int kDefaultHandleWidth = 24;
constexpr int kDefaultHandleHeight = 200;
constexpr int kDefaultCardHeight = 200;
constexpr qreal kDefaultPosition = 1.0; // bottom
const QColor kDefaultAccent(30, 215, 96);
} // namespace

AppSettings &AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings()
{
    QSettings s;
    s.beginGroup("ui");
    m_scale = std::clamp(s.value("scale", kDefaultScale).toDouble(), 0.7, 1.6);
    m_handleWidth = std::clamp(s.value("handleWidth", kDefaultHandleWidth).toInt(), 12, 48);
    m_handleHeight = std::clamp(s.value("handleHeight", kDefaultHandleHeight).toInt(), 40, 400);
    m_cardHeight = std::clamp(s.value("cardHeight", kDefaultCardHeight).toInt(), 100, 480);
    m_rightSide = s.value("rightSide", true).toBool();
    m_position = std::clamp(s.value("position", kDefaultPosition).toDouble(), 0.0, 1.0);
    m_darkTheme = s.value("darkTheme", true).toBool();
    m_accent = QColor(s.value("accent", kDefaultAccent.name()).toString());
    if (!m_accent.isValid())
        m_accent = kDefaultAccent;
    m_language = s.value("language", "auto").toString();
    m_layer = Layer(std::clamp(s.value("layer", int(Layer::AboveWindows)).toInt(), 0, 2));
}

void AppSettings::save() const
{
    QSettings s;
    s.beginGroup("ui");
    s.setValue("scale", m_scale);
    s.setValue("handleWidth", m_handleWidth);
    s.setValue("handleHeight", m_handleHeight);
    s.setValue("cardHeight", m_cardHeight);
    s.setValue("rightSide", m_rightSide);
    s.setValue("position", m_position);
    s.setValue("darkTheme", m_darkTheme);
    s.setValue("accent", m_accent.name());
    s.setValue("language", m_language);
    s.setValue("layer", int(m_layer));
}

#define SETTER(name, member, type)        \
    void AppSettings::name(type value)    \
    {                                     \
        if (member == value)              \
            return;                       \
        member = value;                   \
        save();                           \
        emit changed();                   \
    }

SETTER(setScale, m_scale, qreal)
SETTER(setHandleWidth, m_handleWidth, int)
SETTER(setHandleHeight, m_handleHeight, int)
SETTER(setCardHeight, m_cardHeight, int)
SETTER(setRightSide, m_rightSide, bool)
SETTER(setPosition, m_position, qreal)
SETTER(setDarkTheme, m_darkTheme, bool)
SETTER(setAccent, m_accent, const QColor &)
SETTER(setLanguage, m_language, const QString &)
SETTER(setLayer, m_layer, Layer)

#undef SETTER

void AppSettings::resetToDefaults()
{
    m_scale = kDefaultScale;
    m_handleWidth = kDefaultHandleWidth;
    m_handleHeight = kDefaultHandleHeight;
    m_cardHeight = kDefaultCardHeight;
    m_rightSide = true;
    m_position = kDefaultPosition;
    m_darkTheme = true;
    m_accent = kDefaultAccent;
    m_language = QStringLiteral("auto");
    m_layer = Layer::AboveWindows;
    save();
    emit changed();
}

Theme Theme::current()
{
    const AppSettings &s = AppSettings::instance();
    Theme t;
    t.dark = s.darkTheme();
    t.accent = s.accent();
    if (t.dark) {
        t.bg = QColor(0, 0, 0);
        t.text = QColor(255, 255, 255);
        t.subText = QColor(179, 179, 179);
        t.track = QColor(77, 77, 77);
        t.hover = QColor(255, 255, 255, 20);
        t.line = QColor(255, 255, 255, 45);
    } else {
        t.bg = QColor(250, 250, 250);
        t.text = QColor(20, 20, 20);
        t.subText = QColor(95, 95, 95);
        t.track = QColor(205, 205, 205);
        t.hover = QColor(0, 0, 0, 14);
        t.line = QColor(0, 0, 0, 45);
    }
    return t;
}
