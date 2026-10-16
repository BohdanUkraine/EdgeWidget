#include "app/Localization.h"

#include "app/AppSettings.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QWidget>

namespace {
// Ukrainian source text -> English. Keep in sync with the tr() strings in the sources.
const QList<QPair<const char *, const char *>> kEnglish = {
    // player / library
    { "Нічого не грає", "Nothing playing" },
    { "Запускаю Spotify…", "Starting Spotify…" },
    { "Не вдалося запустити Spotify", "Couldn't start Spotify" },
    { "Підключи Spotify", "Connect Spotify" },
    { "Натисни, щоб увійти й побачити свою бібліотеку", "Click to sign in and see your library" },
    { "Завантажую бібліотеку…", "Loading library…" },
    { "Бібліотека порожня", "Your library is empty" },
    { "Улюблені треки", "Liked Songs" },
    { "Плейлист", "Playlist" },
    { "Альбом", "Album" },
    { "Виконавець", "Artist" },
    { "Записати на NFC-платівку", "Write to an NFC record" },
    { "Програвач не підключено", "The record player is not connected" },
    { "Спершу поклади платівку на програвач", "Put a record on the player first" },
    { "Записую «%1» на платівку", "Writing “%1” to the record" },
    { "Завантажую треки…", "Loading tracks…" },
    { "Не вдалося завантажити треки", "Couldn't load tracks" },
    { "Тут порожньо", "Nothing here" },
    // tray
    { "Про Qt…", "About Qt…" },
    { "Вийти", "Quit" },
    // settings
    { "Налаштування", "Settings" },
    { "Розміри", "Size" },
    { "Панель", "Panel" },
    { "Кнопка: ширина", "Tab width" },
    { "Кнопка: висота", "Tab height" },
    { "Список треків", "Track list" },
    { "Розташування", "Position" },
    { "Край екрана", "Screen edge" },
    { "Зліва", "Left" },
    { "Справа", "Right" },
    { "Висота на екрані", "Height on screen" },
    { "Поведінка", "Behaviour" },
    { "Запускати разом з Windows", "Start with Windows" },
    { "Запускати при вході в систему", "Open at login" },
    { "Під вікнами", "Below windows" },
    { "Поверх вікон", "Above windows" },
    { "Поверх усього", "Above everything" },
    { "Поверх вікон: повноекранні програми й відео перекривають панель.",
      "Above windows: full-screen apps and videos cover the panel." },
    { "0 % — угорі, 100 % — унизу. Якщо над панеллю мало місця, треки відкриваються вниз.",
      "0 % = top, 100 % = bottom. If there is no room above the panel, tracks open downwards." },
    { "Вигляд", "Appearance" },
    { "Тема", "Theme" },
    { "Темна", "Dark" },
    { "Світла", "Light" },
    { "Колір рамки", "Border colour" },
    { "Свій колір", "Custom colour" },
    { "Мова", "Language" },
    { "Про програму", "About" },
    { "Програвач", "Record player" },
    { "Не підключено. Шукаю програвач (емулятор на цьому ПК)…", "Not connected. Looking for the player (emulator on this PC)…" },
    { "Підключено. На рідері платівка %1", "Connected. Record on the reader: %1" },
    { "Підключено. Поклади платівку на рідер", "Connected. Put a record on the reader" },
    { "Авто", "Auto" },
    { "Авто: за країною акаунта Spotify, інакше мова Windows.",
      "Auto: from your Spotify account's country, otherwise the Windows language." },
    { "Оновити бібліотеку", "Refresh library" },
    { "Вийти з акаунта", "Sign out" },
    { "Скинути налаштування", "Reset settings" },
    { "Не підключено. Увійди, щоб бачити свою бібліотеку.", "Not connected. Sign in to see your library." },
    { "Підключено: %1", "Connected: %1" },
    { "Підключено", "Connected" },
    { "Змінити акаунт / Client ID…", "Change account / Client ID…" },
    { "Увійти в Spotify…", "Sign in to Spotify…" },
    // Spotify setup dialog
    { "Підключення Spotify", "Connect Spotify" },
    { "Відкрий <a href='https://developer.spotify.com/dashboard'>developer.spotify.com/dashboard</a> і натисни <b>Create app</b>.",
      "Open <a href='https://developer.spotify.com/dashboard'>developer.spotify.com/dashboard</a> and click <b>Create app</b>." },
    { "Назва та опис — будь-які. У <b>Redirect URIs</b> додай адресу нижче.",
      "Any name and description. Add the address below to <b>Redirect URIs</b>." },
    { "У розділі API познач <b>Web API</b> і збережи.", "Under APIs tick <b>Web API</b> and save." },
    { "Скопіюй <b>Client ID</b> зі сторінки застосунку і встав сюди.",
      "Copy the <b>Client ID</b> from the app page and paste it here." },
    { "Копіювати", "Copy" },
    { "Client ID (32 символи)", "Client ID (32 characters)" },
    { "Увійти в Spotify", "Sign in to Spotify" },
    // Spotify messages
    { "Спершу вкажи Client ID", "Enter your Client ID first" },
    { "Порт %1 зайнятий іншою програмою", "Port %1 is used by another program" },
    { "Spotify підключено", "Spotify connected" },
    { "Не вдалося підключити Spotify", "Couldn't connect Spotify" },
    { "Цю вкладку можна закрити.", "You can close this tab." },
    { "Вхід у Spotify скасовано", "Spotify sign-in was cancelled" },
    { "Spotify: не вдалося отримати токен (%1)", "Spotify: couldn't get a token (%1)" },
    { "Spotify не підключено", "Spotify is not connected" },
    { "Немає з'єднання зі Spotify: %1", "No connection to Spotify: %1" },
    { "Щоб бачити плейлисти й виконавців, перепідключи Spotify (налаштування → Spotify)",
      "To see playlists and artists, reconnect Spotify (Settings → Spotify)" },
    { "Spotify не дозволив відтворення (потрібен Premium?)", "Spotify refused playback (Premium required?)" },
    { "Spotify не дає змінювати гучність цього пристрою віддалено (телефон — тільки на ньому самому)",
      "Spotify doesn't allow changing this device's volume remotely (a phone only on the phone itself)" },
    { "Відкрий Spotify на комп'ютері, телефоні чи в браузері", "Open Spotify on your computer, phone or browser" },
    { "Spotify не зміг увімкнути цей плейлист чи альбом: він недоступний",
      "Spotify couldn't start this playlist or album: it is unavailable" },
};
} // namespace

QString Localization::Dictionary::translate(const char *, const char *sourceText, const char *, int) const
{
    // Context-free: the same Ukrainian text means the same thing everywhere.
    return strings.value(QString::fromUtf8(sourceText));
}

Localization &Localization::instance()
{
    static Localization localization;
    return localization;
}

Localization::Localization()
{
    for (const auto &[uk, en] : kEnglish)
        m_english.strings.insert(QString::fromUtf8(uk), QString::fromUtf8(en));
    // Deployed next to the exe; when run from the build tree, straight from Qt.
    for (const QString &dir : { QCoreApplication::applicationDirPath() + "/translations",              // Windows
                                QCoreApplication::applicationDirPath() + "/../Resources/translations", // macOS
                                QLibraryInfo::path(QLibraryInfo::TranslationsPath) }) {
        if (m_qtUkrainian.load(QStringLiteral("qtbase_uk"), dir))
            break;
    }
    m_country = QSettings().value("spotify/country").toString();
    connect(&AppSettings::instance(), &AppSettings::changed, this, &Localization::apply);
}

QList<QPair<QString, QString>> Localization::languages()
{
    return { { QStringLiteral("uk"), QStringLiteral("Українська") }, { QStringLiteral("en"), QStringLiteral("English") } };
}

void Localization::setSpotifyCountry(const QString &country)
{
    if (country.isEmpty() || country == m_country)
        return;
    m_country = country;
    QSettings().setValue("spotify/country", country);
    apply();
}

QString Localization::resolve() const
{
    const QString chosen = AppSettings::instance().language();
    if (chosen != QLatin1String("auto"))
        return chosen;
    if (!m_country.isEmpty())
        return m_country.compare("UA", Qt::CaseInsensitive) == 0 ? QStringLiteral("uk") : QStringLiteral("en");
    return QLocale::system().language() == QLocale::Ukrainian ? QStringLiteral("uk") : QStringLiteral("en");
}

void Localization::apply()
{
    const QString lang = resolve();
    if (lang == m_current && m_applied)
        return;
    m_current = lang;
    m_applied = true;

    QCoreApplication::removeTranslator(&m_english); // no-op if not installed
    QCoreApplication::removeTranslator(&m_qtUkrainian);
    if (lang == QLatin1String("en"))
        QCoreApplication::installTranslator(&m_english);
    else if (!m_qtUkrainian.isEmpty())
        QCoreApplication::installTranslator(&m_qtUkrainian);

    // Hand-painted views call tr() while painting: just repaint them.
    for (QWidget *w : QApplication::allWidgets())
        w->update();
    emit languageChanged();
}
