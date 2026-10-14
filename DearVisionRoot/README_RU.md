# DearVisionRoot — internal ARM64

Пакет: `com.catsbit.oxidesurvivalisland`. Дамп: `1.14.11949`. Архитектура: `arm64-v8a`.
Это **внутренняя `libDear.so` с root-загрузчиком**, не APK и не external-оверлей.

## Что готово и что нет

Перенесены реализованные части Encore: меню, ESP/стрелки, Aim, эксперимент origin,
FreeCam, FOV/Aspect, scoped No Recoil / No Spread / Arms FOV, визуальные Chams,
профили, ввод и renderer HitEffect с preview/API.

**Не все пункты Encore имели backend. Полная реализация всех пунктов меню НЕ
заявляется.** Source-less функции обозначены `NO BACKEND`; список и точные
ограничения — **[MODULES_RU.md](MODULES_RU.md)**. Прочитай его до включения модулей.
В частности, origin override не подтверждён как рабочий Silent Aim; визуальные
Chams не меняют шейдеры, а `On-screen Only` не проверяет стены — для Aim добавлен отдельный `Visible Check`.

Android ARM64-сборка проверяется GitHub Actions. Успешная компиляция не является
проверкой хуков/эффектов на телефоне. **Тестов на устройстве не было.**

## ESP и Aim Bot: настройки

- **Visuals / ESP**: существующие рамки, линии, ID, дистанция, счётчик и стрелки.
- **Combat / AimBot / AimBot Enable**: включает наведение на ближайшую к центру подходящую цель.
- **FOV radius (px)**: радиус выбора цели от центра Unity Screen, 30–400 пикселей,
  не угол в градусах. `FOV Check` включает ограничение, `Draw FOV` рисует круг.
- **Smooth (0=snap)**: 0 — мгновенно; 1–100 — экспоненциальное сглаживание
  с учётом времени кадра. Чем больше число, тем медленнее наведение.
- **Visible Check**: `Physics.Linecast` от камеры к выбранной точке. Цель
  допускается, если препятствия нет либо первое попадание — в коллайдер её
  Transform/дочернего Transform. Проверка выполняется в игровом Update, не renderer.
  Если нужный Physics API недоступен, при включённой проверке цели не выбираются.
- **On-screen Only** — независимый старый фильтр границ экрана, не проверка стен.
- **Scope Only** по умолчанию включён: для наведения нужно прицелиться или
  отключить этот переключатель. Max Distance и Bone дополнительно ограничивают выбор.

Настройки сохраняются в профилях; новый ключ — `aim.visibleCheck`. Старый
`aim.screen` по-прежнему управляет только экранным фильтром. Visible Check по
умолчанию выключен; ESP и Aim тоже не включаются автоматически при старте.

**Ограничения Visible Check:** используются все физические слои, триггеры
игнорируются. Это проверка коллайдеров, не прозрачности материалов: стекло,
собственный коллайдер или служебная геометрия могут блокировать цель; геометрия
без коллайдера не блокирует. Луч изнутри препятствия имеет ограничения Unity
Physics. Кости, кроме Head, остаются приблизительными. Фильтр относится к
обычному Aim, не скрывает ESP и не меняет эксперимент Origin Override.
RVA и layout RaycastHit сверены с дампом; ABI и поведение Physics на устройстве
ещё не проверены.

## Два архива

После успешного CI доступны артефакты:

- `DearVisionRoot-arm64-untested-on-device`: `libDear.so`, `dear-loader`,
  `run-root.sh`, инструкции, лицензии, `BUILD.txt` и `SHA256SUMS`;
- `DearVisionRoot-source-with-dump`: ZIP исходников проекта, CI и актуального дампа.
  Без `.git`, host-test бинарников и промежуточных сборок. Статический `libdobby.a`
  из исходного DearVision включён как необходимая зависимость.

`BUILD.txt` фиксирует commit и NDK конкретной сборки. Оба архива создаются из
одного commit. GitHub может потребовать вход для скачивания Actions artifacts.

## Сборка исходников

Требуются Python 3, Bash и Android NDK r27+. Распакуй source ZIP и выполни:

```sh
python3 DearVisionRoot/tools/offsets.py
NDK_PATH=/path/to/android-ndk DearVisionRoot/build.sh
```

Результат: `DearVisionRoot/build/libs/arm64-v8a/libDear.so` и `dear-loader`.
Исходные старые `libDear.so` из архивов не используются.

## Проверки на компьютере

```sh
DearVisionRoot/tools/test.sh
```

Проверяются 24 RVA и 32 поля по дампу, namespace/перегрузки, отличия RVA от
Offset, callbacks и snapshots, отключение/восстановление origin и камеры,
scoped weapon/FOV overrides, математика Aim, FOV/дальность/Visible Check с mock Physics,
схема/границы/атомарность загрузки
профилей, события эффектов, меню portrait/landscape и root-launcher guards.
Это host-тесты с mock API, **не эмуляция Android/Unity**.

## Запуск бинарной сборки

1. Распакуй бинарный артефакт. Открой точно соответствующую дампу версию игры
   с **OpenGL ES 3**. Лучше сначала дождаться игрового экрана, затем загружать `.so`.
2. Скопируй три файла на телефон:

```sh
adb shell mkdir -p /data/local/tmp/DearVisionRoot
adb push libDear.so dear-loader run-root.sh /data/local/tmp/DearVisionRoot/
adb shell chmod 755 /data/local/tmp/DearVisionRoot/dear-loader
adb shell chmod 644 /data/local/tmp/DearVisionRoot/libDear.so
adb shell su -c 'sh /data/local/tmp/DearVisionRoot/run-root.sh --check'
adb shell su -c 'sh /data/local/tmp/DearVisionRoot/run-root.sh --load'
adb logcat -s DearVision
```

3. Открой `DearVision / INTERNAL ROOT`. Функции, изменяющие игровой процесс,
   по умолчанию выключены. Настройки профиля могут включить ранее сохранённые функции.
4. `PAUSE / HIDE` выключает подключённые модули, не завершает игру. Восстановление
   игровых полей/камеры выполняется на следующем игровом callback. Для замены
   библиотеки или её выгрузки полностью перезапусти игру — hot-unload не поддержан.

Профили: кнопка **Profiles / effect preview**, New profile / Save / Load / Delete.
Хранятся внутри игры в `files/dearvision/configs`, не `/data/local/tmp`.
F10/Insert переключают меню при доступном keyboard input hook. FreeCam: два
multitouch-стика, UP/DN. Касания продолжают приходить и в игру.

## Существенные ограничения

- Root не гарантирует ptrace, доступ игрового процесса к `.so` или разрешение
  `dlopen` в linker namespace. Загрузчик выводит диагностические ошибки и **не
  обходит SELinux**. При необходимости явно задай уже доступный игре путь:
  `DEAR_LIBRARY_PATH=/absolute/allowed/path/libDear.so` перед запуском скрипта.
- Скрипт отказывается от другого/неизвестного versionName. Сверь версию и дамп,
  не меняй проверку вслепую. Совпадение номера версии не подтверждает хеш libil2cpp.
- Загрузчик экспериментальный: приостанавливает один поток, сохраняет GPR/FPSIMD,
  вызывает mmap/dlopen и восстанавливает регистры. Lock/deadlock внутри dlopen
  нельзя сделать безопасным одним восстановлением регистров. После timeout/сбоя
  **перезапусти игру**. SVE не сохраняется; такие устройства не валидированы.
- Только OpenGL ES 3 / eglSwapBuffers. Vulkan не отключается принудительно.
- Private input symbols зависят от Android/OEM; при недоступности покажется ошибка.
  Движок остаётся привязан к первому EGL context; после его пересоздания может
  потребоваться перезапуск. Смена surface в том же context поддержана.
- Нет гарантии эффективности игровых модулей, совместимости с прошивкой или
  допустимости модификаций по правилам игры. Runtime-проверка обязательна.

## Обновление дампа

```sh
python3 DearVisionRoot/tools/offsets.py path/to/new/dump.cs --write
```

Внутренний адрес: **ELF load bias + RVA**, не base + file Offset. Помимо таблицы
нужно пересмотреть сигнатуры/ABI, поля, маркировку версии, `Target.h` и guard в
`run-root.sh`. Дамп не предоставляет доказательства семантики обфусцированных полей.
