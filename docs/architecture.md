# Архітектура

## Папки й шари

```
src/
  main.cpp      створює об'єкти й з'єднує їх
  app/          база: налаштування й тема, мова, спільні типи даних (MediaState)
  ui/           все, що бачить користувач: панель, її вміст, діалоги, трей
    library/    бібліотека Spotify: список, картки над панеллю, спільне малювання
  device/       програвач MediaSpin: протокол, з'єднання, синхронізація з відтворенням
  playback/     що грає зараз і як цим керувати; гучність
  spotify/      Spotify Web API, вхід, запуск і кеш десктопного застосунку
  platform/     що відрізняється між ОС: інтерфейси (*.h) і спільне
    win/ mac/ linux/   реалізації
```

- **Залежності в один бік:** `ui → device → playback → spotify → platform → app`. Нижній шар не знає
  про верхні.
- **Код під конкретну ОС — тільки в `platform/win|mac|linux`.** Решта без `#ifdef` платформи; спільний
  інтерфейс — у `platform/*.h`.
- **Інклуди від `src/`** з назвою папки: `#include "spotify/SpotifyClient.h"`.
- Новий файл — у `qt_add_executable` в `CMakeLists.txt` поруч зі своїм шаром.
- Коментарі в коді — короткі й лише там, де без них незрозуміло, чому так. Як працює програма — тут, у `docs/`.
- Рядки інтерфейсу — українською в `tr()`; англійський переклад — у словнику `app/Localization.cpp`
  (нова мова — новий словник там же, у списку налаштувань з'явиться сама).

## Класи

| Папка | Клас | Що робить |
|---|---|---|
| `app` | `AppSettings` | налаштування + тема (`Theme`), `uiScale()` |
| `app` | `Localization` | мова інтерфейсу: словник від українських рядків `tr()` |
| `app` | `MediaState.h` | що грає зараз (спільне для playback, spotify і ui) |
| `ui` | `EdgePanel` | вікно на краю екрана: язичок, виїзд, позиція, ховання при втраті фокуса |
| `ui` | `PanelContent` | вміст панелі: компонує й з'єднує плеєр, бібліотеку, картки, таби, налаштування |
| `ui` | `PanelLayout.h` | розмітка панелі (усі розміри в одному місці) |
| `ui` | `ScaledWidget.h` | база для намальованих віджетів: логічні координати × масштаб |
| `ui` | `PlayerView` | платівка, кнопки, поточний трек і його плейлист, прогрес |
| `ui` | `TabStrip`, `SettingsPage` | права смуга (бібліотека, фільтри, налаштування); сторінка налаштувань |
| `ui` | `PlaylistScrubPopup` | прев'ю треків над панеллю, коли фейдер у режимі «Плейлист» |
| `ui` | `VolumeOverlay` | шкала гучності Spotify в правому нижньому куті |
| `ui` | `NowPlayingPopup` | картка «Зараз грає» в правому нижньому куті, коли платівку поклали при згорнутій панелі |
| `ui` | `ErrorToasts` | помилки картками вгорі панелі |
| `ui` | `SpotifySetup`, `TrayIcon`, `VinylIcon` | вікно підключення Spotify; трей; малюнок платівки (і `.ico` / `.icns` через `scripts/make-icon.ps1`) |
| `ui/library` | `LibraryView` | список бібліотеки: папки й виконавці (їхні альбоми з API) сторінками зі слайдом, фільтр |
| `ui/library` | `LibraryCard` | картка над панеллю: назва відкритої папки, фільтри |
| `ui/library` | `TrackListPopup` | картка з треками плейлиста / альбому |
| `ui/library` | `LibraryStyle` | спільне малювання рядків, кеш обкладинок, скрол жестом |
| `device` | `MediaSpinProtocol` | protobuf `MessageRequest` вручну (без libprotobuf) |
| `device` | `BleLink` | BLE до MediaSpin (Qt Bluetooth): пошук, підключення з таймаутом, підтвердження команд, перевірка адаптера |
| `device` | `DeviceController` | команди програвача → відтворення (спершу переносить його на цей ПК); трек / пауза / перемотка → програвач; колір платівки |
| `playback` | `MediaController` | Spotify на цьому ПК (`LocalPlayer`), запасний Web API, автозапуск, оптимістичний стан |
| `playback` | `VolumeController` | кроки й mute крутилки → гучність системи або Spotify |
| `spotify` | `SpotifyAuth` | вхід OAuth PKCE, токени (`SecretStore`) |
| `spotify` | `SpotifyClient` | Web API: бібліотека, треки, відтворення, гучність, перенесення на цей ПК |
| `spotify` | `SpotifyLauncher` | тихий запуск десктопного Spotify (стан «запускаю…») |
| `spotify` | `SpotifyLocalLibrary` | закріплені й папки: LevelDB-кеш десктопного Spotify |
| `platform` | `LocalPlayer` | Spotify на цьому ПК: Windows — медіасесії (GSMTC), macOS — AppleScript, Linux — MPRIS |
| `platform` | `WindowLayer` | шар вікна: під / поверх вікон / поверх усього |
| `platform` | `SecretStore` | секрети: Windows — DPAPI, macOS — Keychain, Linux — libsecret |
| `platform` | `SpotifyApp` | чи запущено Spotify, тихий запуск, де лежать його дані |
| `platform` | `BluetoothSupport` | чи є адаптер з Bluetooth LE і чи ввімкнений (Windows; на macOS / Linux — «невідомо») |
| `platform` | `Autostart` | Windows — `HKCU\…\Run`, macOS — LaunchAgent, Linux — `~/.config/autostart` |
| `platform` | `AppVolume` | системна гучність і гучність Spotify у мікшері Windows (без входу в Spotify) |
| `platform` | `AppInit` | до `QApplication`: Linux під Wayland іде через XWayland |
| `platform` | `CoverFetcher` | обкладинка за https-URL (macOS, Linux) |

## Spotify

- Client secret не зберігається (PKCE); refresh token шифрується (`SecretStore`). Qt NetworkAuth
  навмисно не використовується — він під GPL.
- Web API не віддає ні папок, ні закріплених. `SpotifyLocalLibrary` читає їх з кешу десктопного
  Spotify: `<дані Spotify>/Users/<id>-user/primary.ldb` (LevelDB, блоки Snappy). Ключі:
  `!col#ylpin#<uri>` — закріплені (значення: protobuf із часом закріплення), `!pl#slc#<rootlist>` —
  список плейлистів з дужками `spotify:start-group:<id>:<назва>` / `spotify:end-group:<id>`. Формат
  недокументований: що не розпізналось, просто пропускається. Свіжі зміни лежать у `*.log`, який
  Spotify тримає заблокованим, тож у віджет вони потрапляють після того, як Spotify їх ущільнить.
- Порожні плейлисти: якщо `total` 1–100, один запит `/playlists/{id}/items` перевіряє, чи є там треки.
  Результат кешується за `snapshot_id` у `trackless.json`.
- «Останній плейлист»: коли активної сесії немає, підсвічується `spotify/lastContext`, якщо в Spotify
  на ПК завантажений `spotify/lastTrack`.
- Перенесення на цей ПК: пристрій з назвою, як у комп'ютера, або єдиний «Computer», поки Spotify
  запущений тут. Чужий ноутбук віджет не будить.

## Програвач MediaSpin

Протокол — `shared/PROTOCOL.md`, схема — `shared/proto/api/request.proto` (папка `shared` поруч із
`widget`, не в цьому репозиторії). Тільки Bluetooth LE, сервіс Nordic UART; захисту поки немає.
Кожну команду програвач підтверджує, без підтвердження — до 4 спроб. Якщо за 10 с після підключення
notify так і не ввімкнувся, з'єднання рветься й починається заново. Адаптер перевіряється один раз
при запуску (`BluetoothSupport`): без нього чи без Bluetooth LE пошуку немає зовсім.
