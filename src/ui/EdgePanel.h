#pragma once

#include <QPropertyAnimation>
#include <QWidget>

#include <optional>

class DeviceController;
class MediaController;
class PanelContent;
class SpotifyClient;

// Frameless panel docked to a screen edge (side, size, position and window layer come
// from AppSettings and apply live).
// Collapsed: only the handle with the chevron is visible at the edge.
// Expanded: the panel slides out from behind the edge (Android edge-panel style).
class EdgePanel : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal reveal READ reveal WRITE setReveal)

public:
    EdgePanel(MediaController *media, SpotifyClient *spotify, DeviceController *device, QWidget *parent = nullptr);

    qreal reveal() const { return m_reveal; }
    void setReveal(qreal value);


public slots:
    void expand();
    void collapse();
    void toggle();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override; // Space = play / pause

private:
    enum class Drag { None, Pending, Horizontal, Vertical };

    void animateTo(bool expanded);
    void applyGeometry();
    QRect availableArea() const;
    QRect handleRect() const;
    QSize panelSize() const;       // expanded content size in px
    int handleHeight() const;      // collapsed height in px
    qreal centerY(qreal position) const;
    qreal positionForCenter(qreal center) const;
    bool rightSide() const;
    int inwardDx(int dx) const;    // positive = towards the screen centre
    bool ownsWindow(QWidget *window) const;

    qreal m_reveal = 0.0;
    bool m_expanded = false;
    std::optional<qreal> m_dragPosition; // live position while dragging the handle

    MediaController *m_media;
    PanelContent *m_content = nullptr;
    QPropertyAnimation m_anim;

    Drag m_drag = Drag::None;
    QPoint m_pressGlobal;
    qreal m_pressCenter = 0.0;
    qreal m_pressReveal = 0.0;
};
