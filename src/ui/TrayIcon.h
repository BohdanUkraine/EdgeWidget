#pragma once

#include <QMenu>
#include <QSystemTrayIcon>

class EdgePanel;

// Tray icon (a small vinyl record): left click shows / hides the panel, the menu only has
// Quit (everything else is in the panel's settings). Follows the theme and the language.
class TrayIcon : public QSystemTrayIcon
{
    Q_OBJECT

public:
    explicit TrayIcon(EdgePanel *panel, QObject *parent = nullptr);

    static QIcon vinylIcon();

public slots:
    void notify(const QString &message);

private:
    void fillMenu();
    void styleMenu();

    QMenu m_menu;
};
