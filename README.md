# DearVision / Oxide

Разрабатываемая **internal ARM64 root-база**: [`DearVisionRoot`](DearVisionRoot/README_RU.md).

- `libDear.so`: внутренние хуки/ESP и меню Encore.
- `dear-loader`: экспериментальный root-загрузчик, не external-оверлей.
- Оффсеты проверены по дампу `1.14.11949`.
- Host-тесты и Android ARM64-сборка прошли в GitHub Actions; работа на устройстве ещё не проверена.
- Остальные игровые модули Encore ещё не портированы и заблокированы в меню.

Исходные `@dearvision.zip`, `EncoreDLC3.zip` и `dump 1.14.11949.zip` сохранены без изменений.
