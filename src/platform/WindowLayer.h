#pragma once

#include <QObject>
#include <QTimer>

class QWidget;

// Keeps a top-level window in the layer AppSettings::layer() asks for:
// below other windows, above them (full-screen apps cover it) or above everything.
// The common part is in platform/WindowLayer.cpp, apply() per OS.
class WindowLayer : public QObject
{
    Q_OBJECT

public:
    explicit WindowLayer(QWidget *window); // follows AppSettings and the window being shown

    void apply();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *m_window;
    QTimer m_poll;          // Windows: watches for full-screen apps
    quintptr m_applied = 0; // last applied state, to skip repeated OS calls (0 = none yet)
};
