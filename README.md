# EdgeWidget

Міні-плеєр Spotify у панелі на краю екрана, у стилі Android Edge Panel (Qt 6 / C++).
Windows 10/11; порт на macOS 12+ — у тому ж коді, див. «Збірка на macOS».

- на краю екрана видно тільки язичок; клік або свайп всередину — панель виїжджає,
  клік поза нею / `Esc` — ховається; вертикальне перетягування язичка змінює позицію;
- платівка з обкладинкою крутиться під час відтворення, кнопки, прогрес з перемотуванням;
- бібліотека Spotify (улюблені, плейлисти, альбоми, виконавці); клік по назві відкриває
  список треків, який виїжджає з-за панелі;
- налаштування в самій панелі: розміри, позиція, тема й колір рамки, мова, акаунт;
- без запущеного Spotify play тихо запускає його (`Spotify.exe --minimized`); музикою
  на телефоні / колонці віджет керує через Web API.

## Архітектура

### Структура папок

```
src/
  main.cpp      створює об'єкти й з'єднує їх
  app/          база: налаштування й тема, мова, спільні типи даних (MediaState)
  ui/           все, що бачить користувач: панель, її вміст, діалоги, трей
    library/    бібліотека Spotify: список, картка з треками, спільне малювання
  device/       програвач MediaSpin: протокол, з'єднання, синхронізація з відтворенням
  playback/     що грає зараз і як цим керувати
  spotify/      Spotify Web API, вхід, запуск десктопного застосунку
  platform/     що відрізняється між ОС: інтерфейси (*.h) і спільне
    win/        реалізації для Windows (WinRT, Win32, DPAPI, реєстр)
    mac/        реалізації для macOS (AppleScript, AppKit, Keychain, LaunchAgent)
```

Правила:

- **Залежності йдуть в один бік:** `ui → device → playback → spotify → platform → app`. Нижній шар
  не знає про верхні: `spotify` — про відтворення й інтерфейс, `app` — ні про кого. Новий код
  кладеться в шар, який йому відповідає.
- **Код під конкретну ОС — тільки в `platform/win|mac`.** Решта коду не містить `#ifdef`
  платформи й не підключає `windows.h` / AppKit; спільний інтерфейс — у `platform/*.h`.
- **Інклуди пишуться від `src/`** з назвою папки: `#include "spotify/SpotifyClient.h"`,
  щоб з першого рядка файлу було видно, на який шар він спирається.
- Новий файл треба додати в `qt_add_executable` у `CMakeLists.txt` (у свою групу).

### Класи

| Папка | Клас | Що робить |
|---|---|---|
| `app` | `AppSettings` | налаштування + тема (`Theme`) |
| `app` | `Localization` | мова інтерфейсу: словник від українських рядків `tr()` |
| `ui` | `EdgePanel` | вікно на краю екрана: язичок, виїзд, позиція, ховання при втраті фокуса |
| `ui` | `PanelContent` | вміст панелі: компонує й з'єднує плеєр, бібліотеку, таби, налаштування |
| `ui` | `PanelLayout.h` | розмітка панелі (усі розміри в одному місці) |
| `ui` | `ScaledWidget.h` | база для намальованих віджетів: логічні координати × масштаб |
| `ui` | `PlayerView` | платівка, кнопки, поточний трек, прогрес |
| `app` | `MediaState.h` | що грає зараз (дані, спільні для playback, spotify і ui) |
| `ui` | `TabStrip`, `SettingsPage` | вертикальні таби; сторінка налаштувань |
| `ui` | `SpotifySetup` | вікно підключення (Client ID) |
| `ui` | `TrayIcon` | іконка-платівка в треї |
| `ui` | `VinylIcon` | малюнок платівки для трею, вікна й `resources/EdgeWidget.ico` / `.icns` (`scripts/make-icon.ps1`) |
| `ui/library` | `LibraryView`, `TrackListPopup` | список бібліотеки / картка з треками альбому |
| `ui/library` | `LibraryStyle` | спільне малювання рядків, кеш обкладинок, скрол жестом |
| `device` | `MediaSpinProtocol` | protobuf `MessageRequest` вручну (без libprotobuf), рамки з varint-довжиною |
| `device` | `DeviceLink` | з'єднання з програвачем (зараз TCP до емулятора), перепідключення |
| `device` | `DeviceController` | команди програвача → відтворення; трек / пауза / перемотка → програвач; колір платівки |
| `playback` | `MediaController` | Spotify на цьому ПК (через `LocalPlayer`, інші плеєри ігноруються), запасний Web API, автозапуск, оптимістичний стан |
| `spotify` | `SpotifyAuth` | вхід OAuth PKCE, токени (`SecretStore`) |
| `spotify` | `SpotifyClient` | Spotify Web API: бібліотека, треки, відтворення |
| `spotify` | `SpotifyLauncher` | тихий запуск десктопного Spotify (стан «запускаю…») |
| `platform` | `LocalPlayer` | Spotify на цьому комп'ютері: Windows — медіасесії (GSMTC), macOS — AppleScript Spotify |
| `platform` | `WindowLayer` | шар вікна: під / поверх вікон / поверх усього; Windows — Win32 + повний екран, macOS — рівні NSWindow |
| `platform` | `SecretStore` | секрети: Windows — DPAPI у QSettings, macOS — Keychain |
| `platform` | `SpotifyApp` | чи запущено Spotify і тихий запуск (`--minimized` / приховано у фоні) |
| `platform` | `Autostart` | автозапуск: Windows — `HKCU\…\Run`, macOS — LaunchAgent |
| `platform` | `AppVolume` | гучність Spotify у мікшері Windows (запасний варіант без входу); на macOS немає |

Нова мова — словник у `app/Localization.cpp` (з'явиться в списку налаштувань сама).

### Підключення Spotify

Кожен користувач підключає **свій** застосунок Spotify (як у Home Assistant), тому ліміт
режиму розробки на 5 людей його не стосується. Для керування відтворенням потрібен Premium.

1. https://developer.spotify.com/dashboard → **Create app**.
2. Redirect URI: `http://127.0.0.1:8888/callback`, API: **Web API**.
3. Панель → шестерня → Spotify → «Увійти в Spotify…» → вставити Client ID → увійти в браузері.

Client secret не зберігається; refresh token шифрується Windows DPAPI. Qt NetworkAuth навмисно
не використовується (GPL).

### Програвач MediaSpin

Протокол — [`shared/PROTOCOL.md`](../shared/PROTOCOL.md), схема — `shared/proto/api/request.proto`.
Поки BLE не підключено, віджет говорить з емулятором по TCP `127.0.0.1:47800`:

```
pip install protobuf
python ../emulator/mediaspin_emulator.py
```

- клік по платівці в емуляторі — «поклав на рідер»: платівка у віджеті фарбується в заводський
  колір з мітки (навіть порожня), а якщо на ній записано альбом / плейлист — він вмикається;
- записати платівку: поклади її на рідер, потім кнопка NFC у бібліотеці віджета
  (записується тільки посилання, колір змінити не можна);
- кнопки, гучність і фейдер емулятора керують Spotify; трек, пауза й перемотка у віджеті
  рухають фейдер, а шафл і повтор запалюють лампочки;
- гучність телефона Spotify віддалено міняти не дає: регулятор працює, коли музика грає на ПК.

## Збірка (Windows 10/11)

### 1. Інструменти — один раз

```bash
powershell -ExecutionPolicy Bypass -File scripts\setup-dev.ps1
```

Ставить через winget / aqtinstall (вже встановлене пропускає, можна запускати повторно):

- **Visual Studio 2022 Build Tools** — компілятор MSVC, Windows SDK (з C++/WinRT), CMake;
- **Python 3** — лише щоб поставити Qt;
- **Qt 6.8.3** `msvc2022_64` у `C:\Qt` (тільки qtbase: усе під LGPL).

Ключі: `-WithGit`, `-WithVSCode` — ще й Git / VS Code; `-QtVersion 6.x.y`, `-QtRoot D:\Qt`.
Інсталятор Visual Studio попросить права адміністратора.

### 2. Збірка

```bash
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Run
```

Налаштовує CMake, збирає й кладе в `dist\` готову програму: `EdgeWidget.exe`, DLL-ки Qt і
рантайм MSVC (тож Visual C++ Redistributable на ПК покупця не потрібен). Ключі: `-Config Debug`,
`-Clean` (з нуля), `-Run` (запустити), `-QtDir` (інший Qt).

Те саме вручну:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
cmake --install build --config Release --prefix dist
```

### 3. Інсталер

```bash
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

Збирає програму і робить `out\EdgeWidget-Setup-<версія>.exe` (Inno Setup; поставити один раз:
`winget install --id JRSoftware.InnoSetup -e --scope user`). Версія береться з `project(... VERSION)`
у `CMakeLists.txt`, скрипт інсталера — `installer\EdgeWidget.iss`.

- ставиться без прав адміністратора в `%LOCALAPPDATA%\Programs\EdgeWidget`, майстер українською
  або англійською (за мовою Windows);
- галочки «Запускати разом з Windows» (той самий запис, що й у налаштуваннях віджета) і ярлик
  на робочому столі;
- закриває запущений віджет перед оновленням; деінсталятор прибирає автостарт, а налаштування
  й вхід у Spotify лишає на випадок перевстановлення.

`AppId` в `.iss` не змінювати: за ним оновлення знаходить уже встановлену програму.

## Збірка на macOS

> **Стан:** код порту написаний, але ще жодного разу не компілювався й не запускався
> (писався на Windows). Перша збірка на маку майже напевно покаже дрібні помилки.

Потрібні Xcode Command Line Tools (`xcode-select --install`), CMake і Qt 6.8 для macOS
(онлайн-інсталер Qt, тільки qtbase; збірка universal — Apple silicon + Intel).

```bash
bash scripts/build-mac.sh
```

Збирає `dist-mac/EdgeWidget.app` (macdeployqt кладе фреймворки Qt усередину) і
`dist-mac/EdgeWidget-<версія>.dmg` (перетягнути в «Програми»). Змінні оточення:
`QT_DIR` (за замовчуванням `~/Qt/6.8.3/macos`), `SIGN_IDENTITY` (`Developer ID Application: …`;
без неї — ad-hoc підпис, програма запуститься лише на цьому маку), `NOTARY_PROFILE` (профіль
`xcrun notarytool store-credentials` для нотаризації). Без підпису Developer ID та нотаризації
Gatekeeper не дасть покупцям запустити програму — для продажу потрібен Apple Developer (99 $/рік).

Що працює інакше, ніж на Windows:

- **Spotify на цьому маку** — через AppleScript Spotify: при першому керуванні macOS спитає
  «Edge Widget хоче керувати Spotify» (текст — `NSAppleEventsUsageDescription` в
  `packaging/mac/Info.plist.in`). Стан опитується раз на секунду (`osascript`), подій немає.
  AppleScript знає тільки «повтор увімк/вимк» — повтор одного треку на маку не вмикається.
- **Гучність** — тільки через Web API (системного мікшера по програмах на маку немає).
- **Вікно** — на всіх робочих столах (Spaces); «поверх вікон» = плаваючий рівень,
  повноекранні програми й так у своєму Space; «поверх усього» показується й над ними.
- **Без іконки в Dock** (`LSUIElement`), іконка-платівка — у рядку меню.
- **Автостарт** — LaunchAgent `~/Library/LaunchAgents/com.edgewidget.EdgeWidget.plist`.
- **Токен Spotify** — у Keychain (служба `EdgeWidget`).

Іконка `resources/EdgeWidget.icns` генерується разом з `.ico` (`scripts/make-icon.ps1`) і
лежить у репозиторії, тож на маку її робити не треба.

## Як продавати без комерційної ліцензії Qt (LGPLv3)

Qt Core / Gui / Widgets доступні під **LGPLv3**, тож закриту програму можна
продавати, якщо виконати умови:

1. **Тільки динамічна лінковка.** Qt — окремі `Qt6*.dll` поряд з exe (як робить
   `cmake --install` / `windeployqt`). Статичну збірку Qt не використовуй.
2. **Не підключай модулі, які в open-source версії тільки під GPL** —
   наприклад Qt Charts, Qt Data Visualization / Graphs, Qt Virtual Keyboard,
   Qt Quick 3D, Qt Quick 3D Physics, Qt Lottie, Qt CoAP/MQTT/OPC UA, інструменти
   Qt Design Studio/Squish тощо. Перед додаванням модуля перевір його ліцензію на
   https://doc.qt.io/qt-6/qtmodules.html. `Qt6::Widgets` — безпечно.
3. **Поклади тексти ліцензій** у `licenses/` (`LGPL-3.0.txt` і `GPL-3.0.txt`;
   вони є в теці інсталяції Qt, напр. `C:/Qt/Licenses`, або на gnu.org) —
   CMake скопіює їх у `dist/licenses`.
4. **Повідом користувача**, що програма використовує Qt під LGPLv3:
   `THIRD_PARTY_NOTICES.txt` (впиши версію Qt і контакт) + кнопка «Про Qt…» у налаштуваннях.
5. **Дай доступ до вихідного коду Qt** тієї версії, що постачаєш (посилання на
   download.qt.io або пропозиція надіслати на запит). Якщо змінюєш сам Qt —
   ці зміни теж треба публікувати. Свій код відкривати **не** треба.
6. **В EULA не забороняй** користувачам заміну Qt DLL та реверс-інжиніринг
   у межах, потрібних для налагодження таких змін.
7. Не називай продукт так, ніби він від Qt (торгова марка «Qt»).

Це не юридична консультація; для серйозного продажу варто глянути
https://www.qt.io/licensing/open-source-lgpl-obligations.
