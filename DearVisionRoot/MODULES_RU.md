# Матрица переноса Encore → internal

Все статусы ниже описывают **реализацию в исходниках**, а не подтверждённую работу
в игре. Для этой версии дампа не было проверки на устройстве.

| Модуль исходного Encore | Что сделано в internal | Ограничения |
|---|---|---|
| LitvinMenu / темы / анимации | Перенесены, настройки привязаны к internal-модулям | Не все пункты исходного меню имели backend |
| ESP box, line, fill, ID, distance, count | Unity projection в Update, snapshot в renderer | ID — не nickname; счётчик обновлённых объектов, не всего сервера |
| ESP arrows | Добавлена отрисовка направлений для целей вне экрана | Требуется свежий snapshot |
| Aim | Выбор цели, FOV, сглаживание, дальность, scope, шесть точек | Head — worldCameraRoot при валидных данных; другие точки приблизительные. `On-screen Only` НЕ line-of-sight |
| SilentAim из исходника | Перенесён как **Origin Override (EXPERIMENT)**, с исправленным восстановлением | Исходник подменяет `lastTickPosition`, а не траекторию/выстрел. Это НЕ доказанный Silent Aim/Magic Bullets |
| No Recoil / No Spread | Исправлена цепочка config; scoped override вокруг локального `FPHitscan.Update` | Только этот callback/type; выстрелы вне него могут не измениться. После callback значения восстановлены |
| FOV Arms | Scoped override `FPObject.normalFOV/aimFOV` в `FPManager.LateUpdate` | Поля `int`, не `float`; после callback восстановлены |
| Custom Camera FOV / Aspect | Unity Camera API, восстановление при отключении | Игра может переписать значения позже в кадре |
| FreeCam | Unity Transform API, поза, скорость, multitouch-стики, вверх/вниз, восстановление | Визуальная камера, не полёт тела игрока. Не блокирует ввод в игру |
| Chams | Перенесён исходный DrawList renderer: Flat / Glow / Wireframe | Это прямоугольники/свечение, НЕ shader/material chams; нет проверки стен |
| Bind | Android input events вместо недоступного из app UID `/dev/input`; F10/Insert, mouse state | Клавиатура доступна при успешном consume-hook; fallback — только touch |
| Configs | Save/load/delete/new, профили в private files, schema/range validation | Формат internal v1, не автоматический импорт старых внешних `.cfg`; путь рассчитан на Android user 0 |
| HitEffect | Перенесён renderer, preview-панель, thread-safe `DearVisionNotifyHit` API | В Encore отсутствуют вызывающие `onHit/onKill` игровые хуки. Автоматических эффектов нет |

## Пункты, для которых в Encore нет реализации

В предоставленном архиве следующие переключатели есть в `Vars`/меню, но не имеют
реализованного соответствующего backend, который можно перенести:

- Health / Armor / Ammo bars;
- Wallshot и Infinity Ammo;
- Bunny Hop, Strafer, Fast Knife, Air Jump, Invisible;
- Buy Anywhere, Infinity Buy;
- Sky Change, Host Indicator, Skip Phase, Cancel Match;
- Infinity Grenades, Fast Detonation, No Grenade Damage;
- автоматические hit/kill callbacks для HitEffect.

Эти строки помечены **NO BACKEND**, не вызывают фиктивные функции и не включаются
через профили. Чтобы они заработали, нужна **новая реализация**, а не механический
перенос или смена оффсетов. Поэтому перенос имеющегося кода **не означает**, что
все пункты меню стали рабочими. Этот пакет не объявляется полной реализацией всех
показанных в Encore возможностей.

## Исправления относительно исходного external

- `FPHitscan.config`: **0x130**; по старому 0x128 находится `signalProjectile`.
- `HitscanWeaponConfig.dispersionConfig`: **0x68**; по старому 0x60 — `ballistic`.
- `HitscanWeaponConfig.recoilConfig`: **0x70**, а не 0x68.
- `MouseLook.qxz`: 0x4C (x=pitch, y=yaw по исходной логике); не путать с `qxq` по 0x70.
- Эксперимент origin восстанавливает сохранённое поле на следующем Update даже
  после отключения, потери цели или открытия меню, а также перед OnDisable.
- Сохранённое поле старого local avatar не записывается в новый avatar.
- FreeCam больше не трактует native `m_CachedPtr` как managed GameObject.
- No Recoil / No Spread / Arms FOV не сохраняют указатели на конфиги между кадрами.
- Никаких вызовов `exit()` внутри игры, фоновых внешних сканеров или `/dev/input`
  процессов. Пауза отключает модули; выгрузка библиотеки — через перезапуск игры.
