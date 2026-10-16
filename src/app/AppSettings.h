#pragma once

#include <QColor>
#include <QObject>

// User preferences (persisted in QSettings). Every change emits changed(), and all
// views repaint / re-layout live.
class AppSettings : public QObject
{
    Q_OBJECT

public:
    // Where the panel sits among other windows.
    enum class Layer { BelowWindows, AboveWindows, AboveAll }; // AboveWindows: full-screen apps cover it

    static AppSettings &instance();

    qreal scale() const { return m_scale; }              // main panel size, 1.0 = 100 %
    int handleWidth() const { return m_handleWidth; }    // the tab that stays on screen, px
    int handleHeight() const { return m_handleHeight; }  // px
    int cardHeight() const { return m_cardHeight; }      // track list card, logical px
    bool rightSide() const { return m_rightSide; }
    qreal position() const { return m_position; }        // 0 = top of the screen, 1 = bottom
    bool darkTheme() const { return m_darkTheme; }
    QColor accent() const { return m_accent; }
    QString language() const { return m_language; }      // "auto", "uk", "en"
    Layer layer() const { return m_layer; }

    void setScale(qreal value);
    void setHandleWidth(int value);
    void setHandleHeight(int value);
    void setCardHeight(int value);
    void setRightSide(bool value);
    void setPosition(qreal value);
    void setDarkTheme(bool value);
    void setAccent(const QColor &value);
    void setLanguage(const QString &value);
    void setLayer(Layer value);
    void resetToDefaults();

signals:
    void changed();

private:
    AppSettings();
    void save() const;

    qreal m_scale;
    int m_handleWidth;
    int m_handleHeight;
    int m_cardHeight;
    bool m_rightSide;
    qreal m_position;
    bool m_darkTheme;
    QColor m_accent;
    QString m_language;
    Layer m_layer;
};

inline qreal uiScale()
{
    return AppSettings::instance().scale();
}

// Colours of the current theme.
struct Theme
{
    bool dark = true;
    QColor bg;      // panel / card background
    QColor text;
    QColor subText;
    QColor track;   // progress bar groove
    QColor hover;   // row hover background
    QColor line;    // thin separators, outlines
    QColor accent;  // borders, active items

    static Theme current();
    QColor fade(int alpha) const { return QColor(bg.red(), bg.green(), bg.blue(), alpha); }
    static QString css(const QColor &c) // for Qt stylesheets
    {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }
};
