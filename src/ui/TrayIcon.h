#pragma once

#include <QMenu>
#include <QSystemTrayIcon>

class EdgePanel;

class TrayIcon : public QSystemTrayIcon
{
    Q_OBJECT

public:
    explicit TrayIcon(EdgePanel *panel, QObject *parent = nullptr);

    static QIcon vinylIcon();

private:
    void fillMenu();
    void styleMenu();

    QMenu m_menu;
};
