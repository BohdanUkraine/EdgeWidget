#include "ui/TrayIcon.h"

#include "app/AppSettings.h"
#include "ui/EdgePanel.h"
#include "ui/VinylIcon.h"
#include "app/Localization.h"

#include <QApplication>

TrayIcon::TrayIcon(EdgePanel *panel, QObject *parent)
    : QSystemTrayIcon(vinylIcon(), parent)
{
    setToolTip(QStringLiteral("Edge Widget"));
    setContextMenu(&m_menu);
    fillMenu();
    styleMenu();
    connect(&Localization::instance(), &Localization::languageChanged, this, &TrayIcon::fillMenu);
    connect(&AppSettings::instance(), &AppSettings::changed, this, &TrayIcon::styleMenu);
    connect(this, &QSystemTrayIcon::activated, panel, [panel](ActivationReason reason) {
        if (reason == Trigger)
            panel->toggle();
    });
}

void TrayIcon::fillMenu()
{
    m_menu.clear();
    m_menu.addAction(tr("Вийти"), qApp, [] {
        qInfo("[app] quit from the tray menu"); // tells a deliberate exit apart in the logs
        QApplication::quit();
    });
}

void TrayIcon::styleMenu()
{
    const Theme t = Theme::current();
    QColor selected = t.accent;
    selected.setAlpha(45);
    m_menu.setStyleSheet(QStringLiteral("QMenu { background: %1; color: %2; border: 1px solid %3; padding: 4px 0; }"
                                        "QMenu::item { padding: 6px 22px 6px 16px; background: transparent; }"
                                        "QMenu::item:selected { background: %4; color: %2; }")
                             .arg(Theme::css(t.bg), Theme::css(t.text), Theme::css(t.accent), Theme::css(selected)));
}

void TrayIcon::notify(const QString &message)
{
    showMessage(QStringLiteral("Edge Widget"), message, QSystemTrayIcon::Warning, 5000);
}

QIcon TrayIcon::vinylIcon()
{
    QIcon icon;
    for (int size : { 16, 20, 24, 32, 48, 64 })
        icon.addPixmap(QPixmap::fromImage(vinylImage(size)));
    return icon;
}
