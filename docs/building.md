# Збірка

## Windows 10/11

**Інструменти (один раз):**

```bash
powershell -ExecutionPolicy Bypass -File scripts\setup-dev.ps1
```

Ставить Visual Studio 2022 Build Tools (MSVC, Windows SDK з C++/WinRT, CMake), Python 3 (лише щоб
поставити Qt) і Qt 6.8.3 `msvc2022_64` у `C:\Qt` (qtbase + qtconnectivity, усе під LGPL). Повторний
запуск пропускає встановлене. Ключі: `-WithGit`, `-WithVSCode`, `-QtVersion 6.x.y`, `-QtRoot D:\Qt`.

**Збірка:**

```bash
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Run
```

Кладе в `dist\` готову програму з DLL-ками Qt і рантаймом MSVC. Запущений віджет закриває (він
тримає файли). Ключі: `-Config Debug`, `-Clean`, `-Run`, `-QtDir`. CMake шукає в тій Visual Studio,
де він встановлений (не обов'язково в найновішій).

**Інсталер:**

```bash
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

→ `out\EdgeWidget-Setup-<версія>.exe` (Inno Setup; поставити один раз:
`winget install --id JRSoftware.InnoSetup -e --scope user`). Версія — з `project(... VERSION)` у
`CMakeLists.txt`, скрипт — `installer\EdgeWidget.iss`. Ставиться в `%LOCALAPPDATA%\Programs\EdgeWidget`
без прав адміністратора, майстер українською або англійською. `AppId` в `.iss` не змінювати: за ним
оновлення знаходить встановлену програму.

**Чому закрився віджет:** `scripts\watch-widget.ps1` стежить за процесом і пише код виходу, події
Windows і хвіст логу в `out\watch\` (`-Stop` — зупинити).

**Іконка:** `scripts\make-icon.ps1` малює `resources\EdgeWidget.ico` і `.icns` з `VinylIcon`.

## macOS

> На справжньому маку ще не запускався; компіляцію перевіряє лише CI (`build.yml`).

Потрібні Xcode Command Line Tools, CMake і Qt 6.8 для macOS (qtbase + Qt Connectivity).

```bash
bash scripts/build-mac.sh
```

→ `dist-mac/EdgeWidget.app` і `.dmg`. Змінні: `QT_DIR` (типово `~/Qt/6.8.3/macos`), `SIGN_IDENTITY`
(`Developer ID Application: …`; без неї ad-hoc підпис), `NOTARY_PROFILE` (нотаризація). Для продажу
потрібен Apple Developer.

Відмінності: Spotify — через AppleScript (macOS спитає дозвіл; повтор одного треку не вмикається);
гучність Spotify — тільки Web API; вікно на всіх Spaces, без іконки в Dock; автозапуск — LaunchAgent;
токен — у Keychain; перевірки адаптера Bluetooth немає.

## Linux

Перевірено в Ubuntu 22.04 (WSL) під Xvfb з тестовим MPRIS і тестовим програвачем; зі справжнім
Spotify під Linux ще не запускалось.

```bash
QT_DIR=~/Qt/6.8.3/gcc_64 bash scripts/build-linux.sh
```

→ `dist-linux/EdgeWidget-<версія>-x86_64.AppImage`. Потрібні C++17, CMake, Qt 6.5+ і заголовки
libsecret. Збирати на найстарішому дистрибутиві, який треба підтримати (CI — Ubuntu 22.04). Arch:
`packaging/linux/arch/PKGBUILD`. `cmake --install` кладе все за XDG.

Відмінності: Spotify — MPRIS (деякі збірки не віддають шафл / повтор і позицію); під Wayland віджет
іде через XWayland; «поверх вікон» = «поверх усього»; токен — Secret Service або файл налаштувань;
автозапуск — `~/.config/autostart`; на GNOME без AppIndicator немає трею; перевірки адаптера
Bluetooth немає.

## CI

`.github/workflows/build.yml` збирає Windows, macOS і Linux на кожен пуш.
