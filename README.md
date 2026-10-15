# EncoreDLC / Oxide

**Internal ARM64 root-проект** для дампа `1.14.11949`:
[`EncoreDLC3_Internal/README_RU.md`](EncoreDLC3_Internal/README_RU.md).

- Актуальные изменения по `EncoreDLC3_ESP_10FPS_source_fix.zip`: [ESP / Aim / Death / Visible / Silent — статус и проверки](EncoreDLC3_Internal/ENCORE_ESP_FIX_RU.md).
- `libEncore.so`: внутренние хуки, ESP, меню и адаптированные модули Encore.
- `encore-loader`: экспериментальный root-загрузчик, не external-оверлей.
- CI собирает бинарный архив и отдельный архив исходников с дампом.
- **Не все пункты исходного меню имели backend.** Точная готовность и ограничения:
  [`MODULES_RU.md`](EncoreDLC3_Internal/MODULES_RU.md).
- Host-тесты и NDK-сборка не подтверждают работу на телефоне; device-тестов нет.

Исходные `EncoreDLC3_ESP_10FPS_source_fix.zip`, `EncoreDLC3.zip` и `dump 1.14.11949.zip` сохранены без перезаписи.
