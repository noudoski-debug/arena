# EncoreDLC — перенос Litvin Tap Menu

## Что реально перенесено
- Исходный UI из `LITVIN TAP MENU IMGUI.zip` перенесён в `jni/src/LitvinMenu.cpp` и `jni/include/LitvinMenu.h`.
- Старый интерфейс `MenuEternalMenu.cpp` заменён адаптером, который вызывает Litvin Tap UI.
- Сохранено переключение меню через существующий `bind::consumeMenuToggle()` и кнопку закрытия.
- Добавлено плавное появление/закрытие через alpha и вертикальное смещение.
- Добавлен watermark EncoreDLC / LITVIN TAP, нажатие на него переключает меню.
- Размер меню автоматически подстраивается под размер экрана.
- Основные существующие настройки ESP и Aim связаны с `Vars` (ESP box/name/health/distance, host indicator, aim enable/scope/FOV/smooth/bone/FOV display).
- `jni/Android.mk` включает `LitvinMenu.cpp`.

## Ограничения
- Контролы исходного меню, для которых в текущем проекте нет соответствующего поля/реализации (например, Chams, Triggerbot, часть Items/World/HitData), остаются только элементами интерфейса и не заявляются как работающие функции.
- В среде нет Android NDK, поэтому APK/ELF/so не пересобирался. Существующий бинарник, если он есть в `build/`, не содержит эту новую UI-версию.
- Перед использованием нужно собрать проект подходящей версией Android NDK на ARM64 и проверить запуск на устройстве.
