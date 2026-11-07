#pragma once

#include <QColor>
#include <QObject>

class AppSettings : public QObject
{
    Q_OBJECT

public:
    enum class Layer { BelowWindows, AboveWindows, AboveAll };
    enum class FaderMode { Timeline, Playlist };
    enum class VolumeTarget { System, Spotify };

    static AppSettings &instance();

    qreal scale() const { return m_scale; }
    int handleWidth() const { return m_handleWidth; }
    int handleHeight() const { return m_handleHeight; }
    bool handleVisible() const { return m_handleVisible; }
    int cardHeight() const { return m_cardHeight; }
    int scrubPopupHeight() const { return m_scrubPopupHeight; }
    bool rightSide() const { return m_rightSide; }
    qreal position() const { return m_position; }
    bool darkTheme() const { return m_darkTheme; }
    QColor accent() const { return m_accent; }
    QString language() const { return m_language; }
    Layer layer() const { return m_layer; }
    FaderMode faderMode() const { return m_faderMode; }
    VolumeTarget volumeTarget() const { return m_volumeTarget; }

    void setScale(qreal value);
    void setHandleWidth(int value);
    void setHandleHeight(int value);
    void setHandleVisible(bool value);
    void setCardHeight(int value);
    void setScrubPopupHeight(int value);
    void setRightSide(bool value);
    void setPosition(qreal value);
    void setDarkTheme(bool value);
    void setAccent(const QColor &value);
    void setLanguage(const QString &value);
    void setLayer(Layer value);
    void setFaderMode(FaderMode value);
    void setVolumeTarget(VolumeTarget value);
    void resetToDefaults();

signals:
    void changed();

private:
    AppSettings();
    void save() const;

    qreal m_scale;
    int m_handleWidth;
    int m_handleHeight;
    bool m_handleVisible;
    int m_cardHeight;
    int m_scrubPopupHeight;
    bool m_rightSide;
    qreal m_position;
    bool m_darkTheme;
    QColor m_accent;
    QString m_language;
    Layer m_layer;
    FaderMode m_faderMode;
    VolumeTarget m_volumeTarget;
};

inline qreal uiScale()
{
    return AppSettings::instance().scale();
}

struct Theme
{
    bool dark = true;
    QColor bg;
    QColor text;
    QColor subText;
    QColor track;
    QColor hover;
    QColor line;
    QColor accent;

    static Theme current();
    QColor fade(int alpha) const { return QColor(bg.red(), bg.green(), bg.blue(), alpha); }
    static QString css(const QColor &c)
    {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }
};
