#pragma once

#include <QObject>
#include <QTimer>

class QWidget;

class WindowLayer : public QObject
{
    Q_OBJECT

public:
    explicit WindowLayer(QWidget *window);

    void apply();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *m_window;
    QTimer m_poll;
    quintptr m_applied = 0;
};
