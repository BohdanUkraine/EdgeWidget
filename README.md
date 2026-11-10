# EdgeWidget

Міні-плеєр Spotify у панелі на краю екрана (Qt 6 / C++) для програвача MediaSpin.
Windows 10/11; порти на macOS і Linux — у тому ж коді.

```bash
powershell -ExecutionPolicy Bypass -File scripts\setup-dev.ps1   # інструменти, один раз
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Run  # зібрати в dist\ і запустити
powershell -ExecutionPolicy Bypass -File scripts\package.ps1     # інсталер в out\
```

## Документація

- [Як працює програма для користувача](docs/user-guide.md)
- [Архітектура: шари, класи, Spotify, програвач](docs/architecture.md)
- [Збірка: Windows, інсталер, macOS, Linux](docs/building.md)
- [Ліцензія Qt (LGPLv3) і продаж](docs/licensing.md)
- Протокол програвача — `shared/PROTOCOL.md` (поруч із цим репозиторієм)

## TODO

- [ ] macOS: перша збірка й запуск на справжньому маку.
- [ ] Linux: запуск зі справжнім Spotify на робочому столі.
