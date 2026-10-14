# DearVision / Oxide

**Internal ARM64 root-проект** для дампа `1.14.11949`:
[`DearVisionRoot/README_RU.md`](DearVisionRoot/README_RU.md).

- `libDear.so`: внутренние хуки, ESP, меню и адаптированные модули Encore.
- `dear-loader`: экспериментальный root-загрузчик, не external-оверлей.
- CI собирает бинарный архив и отдельный архив исходников с дампом.
- **Не все пункты исходного меню имели backend.** Точная готовность и ограничения:
  [`MODULES_RU.md`](DearVisionRoot/MODULES_RU.md).
- Host-тесты и NDK-сборка не подтверждают работу на телефоне; device-тестов нет.

Исходные `@dearvision.zip`, `EncoreDLC3.zip` и `dump 1.14.11949.zip` сохранены.
