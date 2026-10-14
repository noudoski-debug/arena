# DearVisionRoot — internal ARM64

**Исходники internal-базы, не готовый проверенный APK/бинарник.**
Цель: `com.catsbit.oxidesurvivalisland`, дамп `1.14.11949`, `arm64-v8a`.
Android-сборка и запуск на телефоне пока **не проверены**: в рабочем окружении нет Android NDK и устройства.

## Что перенесено

- Внутренняя библиотека **`libDear.so`** вместо external-процесса и клонера.
- Меню **Encore / LitvinMenu**: разделы, виджеты, анимации и четыре темы.
- Internal ESP: боксы, заливка, линии, цвета, обводка, толщина линий,
  расстояние от камеры, ID игроков и счётчик недавно обновлявшихся игроков.
- Хуки `PlayerManager.Update` и `OnDisable`; Unity-функции вызываются в игровом
  `Update`, а не из потока ImGui. В рендер передаются копии данных, не указатели на игроков.
- Инициализация через ELF constructor: загрузка через `dlopen` не требует `JNI_OnLoad` или Java VM.
- Отдельный экспериментальный root-загрузчик `dear-loader` и скрипт запуска.

**Это не полный перенос всех игровых модулей Encore.** Combat / Aim / SilentAim,
No Recoil / No Spread, Chams, FreeCam, HitFX, изменения камеры, здоровья/брони,
стрелки вне экрана и прочие неподключённые настройки показывают **`NOT PORTED`**.
Они не включают внешние модули, не создают видимость работающих функций и не пишут
в память по старым смещениям. Их исходники остаются в исходном `EncoreDLC3.zip`.

Вместо ошибочного `healthOffset = 0` используется исключение локального игрока,
`respawning`, `kicked` и снятие записи по `OnDisable`. **Надёжная проверка здоровья
пока не портирована** — флаги не гарантируют исключение всех трупов. Высота бокса
приблизительная (1,6 м от Transform), как в исходном DearVision, не точный скелет.
`Player ID` — именно `userID`, не никнейм. Счётчик — записи Update за последние
300 мс, а не гарантированное число всех игроков сервера.

## Сборка

Нужны Python 3, Bash и Android NDK (рекомендуется r27+). Собирается native `.so`
и ELF-загрузчик, **не APK**. Из корня репозитория:

```sh
python3 DearVisionRoot/tools/offsets.py
NDK_PATH=/path/to/android-ndk DearVisionRoot/build.sh
```

Результат:

```text
DearVisionRoot/build/libs/arm64-v8a/libDear.so
DearVisionRoot/build/libs/arm64-v8a/dear-loader
```

NDK-сборка использует явный список файлов: старые external, Vulkan, инжектор
Encore и сохранённые `.bak` не подхватываются wildcard-правилами. Статический
`libdobby.a` взят из предоставленного DearVision (это единственная сохранённая
prebuilt-зависимость); готовый старый `libDear.so` не используется.

В `.github/workflows/dearvision.yml` добавлена CI-сборка с загрузкой артефактов.
Она выполнится после отправки изменений в GitHub; в этой сессии она **не запускалась**.

## Проверки на компьютере

```sh
DearVisionRoot/tools/test.sh
```

Проверяются:
- 9 RVA и 3 поля непосредственно по `dump 1.14.11949.zip`, без распаковки 88 МБ в Git;
- различение namespace/перегрузок, RVA и файловых Offset, отсутствие и дубликаты методов;
- адресация загрузчика по точному пути файла и executable-сегментам `/proc/maps`;
- Update/OnDisable, локальный игрок, respawning, истечение snapshots и копирование данных;
- рендер всех разделов/вкладок меню без GPU в landscape/portrait, блокировка неперенесённых настроек;
- отрицательные сценарии shell-запуска и синтаксис скриптов/загрузчика.

Host-тесты **не доказывают** правильность Android ABI, хуков Dobby, private input
symbols, ptrace или работы в игре. Они не запускают и не инжектируют игру.

## Root-запуск на своём устройстве

1. Сначала собери новые файлы. Не бери старый бинарник из архивов.
2. Размести файлы на телефоне:

   ```sh
   adb shell mkdir -p /data/local/tmp/DearVisionRoot
   adb push DearVisionRoot/build/libs/arm64-v8a/libDear.so /data/local/tmp/DearVisionRoot/
   adb push DearVisionRoot/build/libs/arm64-v8a/dear-loader /data/local/tmp/DearVisionRoot/
   adb push DearVisionRoot/run-root.sh /data/local/tmp/DearVisionRoot/
   adb shell chmod 755 /data/local/tmp/DearVisionRoot/dear-loader
   adb shell chmod 644 /data/local/tmp/DearVisionRoot/libDear.so
   ```

3. Открой **точно соответствующую дампу** версию игры с OpenGL ES 3.
4. Проверь скрипт без загрузки:

   ```sh
   adb shell su -c 'sh /data/local/tmp/DearVisionRoot/run-root.sh --check'
   ```

5. Загрузи библиотеку:

   ```sh
   adb shell su -c 'sh /data/local/tmp/DearVisionRoot/run-root.sh --load'
   adb logcat -s DearVision
   ```

6. В игре нажми `DearVision / INTERNAL ROOT`, затем **Visuals → ESP → ESP Enable**.
   `PAUSE / HIDE` выключает ESP и скрывает меню, **не завершает игру**. Для выгрузки
   библиотеки/обновления сборки полностью перезапусти игру; hot-unload не реализован.

Скрипт отказывается запускаться при другом `versionName`, отсутствии root,
не-ARM64 устройстве и неоднозначном PID. Если игра использует другой формат версии,
не меняй номер вслепую: сначала нужно подтвердить соответствие бинарника дампу.
Дамп сам по себе не содержит хеша `libil2cpp.so`, поэтому **совпадение версии не
является полной проверкой идентичности бинарника**.

### Ограничения root-загрузчика

Это обычный file-backed `dlopen` через ptrace, без memfd-маскировки и без изменений
SELinux, APK, Magisk/KernelSU или настроек системы. Root сам по себе не гарантирует
доступ к ptrace или разрешение `dlopen` в игровом linker namespace.

В частности, приложение может не иметь права читать/исполнять `.so` из
`/data/local/tmp`. При отказе загрузчик выводит `dlerror`, если его удалось прочитать.
Можно явно задать уже подготовленный доступный игре путь:

```sh
su -c 'DEAR_LIBRARY_PATH=/absolute/allowed/path/libDear.so sh /data/local/tmp/DearVisionRoot/run-root.sh'
```

Скрипт **не обходит** ограничения доступа и не делает SELinux permissive.

Загрузчик останавливает один поток, сохраняет general-purpose и FPSIMD регистры,
вызывает mmap/dlopen, затем восстанавливает регистры и отсоединяется. Это
**экспериментальная операция**: поток может держать нужный загрузчику lock, а
таймаут/сбой посреди `dlopen` нельзя сделать полностью безопасным восстановлением
регистров. При ошибке перезапусти игру перед повторной попыткой. Полное SVE-состояние
не сохраняется; устройства/код, использующие SVE, не валидированы.

### Ограничения рендера и касаний

- Поддержан только `eglSwapBuffers` / OpenGL ES 3. Vulkan не блокируется и не
  переключается принудительно. На Vulkan меню не появится.
- Input-хуки используют private Android symbols. На другой версии Android/OEM
  они могут отсутствовать; тогда смотри сообщение и logcat.
- Касания **не подавляются для игры**: взаимодействуй с меню в безопасном игровом
  экране. Полное разделение ввода ещё не портировано.
- В этой версии renderer привязан к первому подходящему EGL context/surface.
  После пересоздания EGL-контекста/поверхности может потребоваться перезапуск игры.
- Нет гарантии совместимости с конкретной прошивкой или допустимости модификации
  по правилам игры. Runtime-проверка на устройстве обязательна.

## Обновление оффсетов

```sh
python3 DearVisionRoot/tools/offsets.py path/to/new/dump.cs --write
```

Скрипт генерирует `jni/include/GameOffsets.h` из точных классов и сигнатур.
**Internal-адрес = ELF load bias + RVA**, а не `base + Offset`. Поля объектов —
отдельные смещения. После смены версии нужно также пересмотреть ABI, поля,
`Target.h`, проверку версии в `run-root.sh` и документацию. Не подставляй адреса
новой версии, сохраняя старую маркировку.
