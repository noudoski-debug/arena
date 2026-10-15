# Библиотека в maps, но меню нет — диагностика bootstrap v1

Наличие libEncore.so в `/proc/PID/maps` и ненулевой handle `dlopen` **не подтверждают**
выполнение constructor, установку хуков или отрисовку меню.
В этой сборке добавлена диагностика, а не доказанное исправление причины отсутствия меню.

## Что изменено

- Метки до/после проверки process cmdline, pthread_create, EGL dlopen/dlsym,
  каждой установки Dobby hook и первого render callback.
- Первые причины пропуска EGL frame (нет контекста, GLES < 3, маленькая surface,
  другой context) записываются один раз, не спамятся каждый кадр.
- Вход в ImGui initialization и первый отправленный frame отмечаются отдельно.
- Input и game hooks запускаются отдельными worker threads: зависание в поиске
  private input symbol не должно препятствовать запуску game bootstrap.
- Все установки Dobby hooks из этих workers сериализованы одним mutex.
- Дублирование startup-сообщений из logcat в private `files/encoredlc/startup.log`.
  Путь учитывает Android user ID. Новый constructor очищает предыдущий файл;
  записи содержат PID, TID и монотонное время. Запись ограничена 1 МиБ.
- Ошибка создания private logfile сообщается в logcat. Symlink logfile не открывается.
- Loader теперь явно пишет, что подтверждён только dlopen, не запуск хуков/renderer.

## Как поставить обновление

Не перезаписывай .so, пока она используется старым игровым процессом.
Сначала под root:

```sh
am force-stop com.catsbit.oxidesurvivalisland
```

Проверь, что `pidof com.catsbit.oxidesurvivalisland` больше не возвращает PID.
Если процесс остался, не копируй библиотеку поверх загруженной.
Распакуй **новый** ARM64 ZIP, замени libEncore.so и encore-loader в каталоге запуска.
Проверь права и метку новой .so. Бинарник не отключает SELinux и не меняет метки автоматически.
Не используй setenforce 0 и не добавляй широкие правила политики ради запуска.

Открой игру, дождись игрового экрана, затем загрузи библиотеку **один раз**:

```sh
sh /data/local/tmp/EncoreDLC3_Internal/run-root.sh --load
```

Вернись в игру на 10–15 секунд. Потом в root-терминале (для Android user 0):

```sh
cat /data/user/0/com.catsbit.oxidesurvivalisland/files/encoredlc/startup.log
```

В первой строке файла должен быть `diagnostic-bootstrap-v1` и новый PID.
Для другого Android user вместо `/data/user/0` нужен его номер.
Если файл отсутствует или нет прав — пришли саму ошибку и вывод:

```sh
logcat -b all -d -s EncoreDLC:V '*:S'
```

## Как читать результат

- `BOOT constructor ...` / `cmdline ...` / `stopped` — выполнение конструктора и process guard.
- `BOOT worker entered` — worker начал исполняться; строки перед/после EGL показывают этап.
- `HOOK ... waiting` без `begin` — ожидается mutex другого hook install.
- `HOOK ... begin` без `end` — вызов Dobby не вернулся; это не доказательство его причины.
- `HOOK ... end rc=...` — результат конкретной установки, 0 означает успешный возврат Dobby.
- `GAME ...` — поиск IL2CPP и установка lifecycle/weapon hooks. Поиск ограничен 120 секундами.
- `INPUT ...` — этап разрешения private symbols и установки input hooks.
- `EGL first ... callback` — перехваченная функция действительно была вызвана.
- `RENDER initialized` / `first ImGui frame submitted` — init/frame достигнуты;
  это ещё не гарантирует, что frame был виден пользователю.

Отсутствие EGL callback само по себе не доказывает Vulkan: могут использоваться
другой EGL entrypoint, другая библиотека/namespace или другой graphics path.
Наличие libvulkan.so в maps также не доказывает активный renderer.
Строки `serialno_prop`, `DontDestroyOnLoad` или поиск Burst-библиотек без связанного
сбоя не являются доказательством проблемы Encore.
