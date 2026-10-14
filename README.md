# DearVision / Oxide

**Internal ARM64 root-проект** для дампа `1.14.11949`:
[`DearVisionRoot/README_RU.md`](DearVisionRoot/README_RU.md).

- `libDear.so`: MENU ONLY при загрузке; игровые хуки/ESP/Aim — только после явного Connect game. Это не external-оверлей.
- `dear-loader`: экспериментальный root-загрузчик, не external-оверлей.
- CI собирает бинарный архив и отдельный архив исходников с дампом.
- **Не все пункты исходного меню имели backend.** Точная готовность и ограничения:
  [`MODULES_RU.md`](DearVisionRoot/MODULES_RU.md).
- Предыдущая сборка на телефоне завершилась с Security ref detected / SIGKILL. Новый двухэтапный запуск проверен только host-тестами, совместимость не гарантируется.

Исходные `@dearvision.zip`, `EncoreDLC3.zip` и `dump 1.14.11949.zip` сохранены.
