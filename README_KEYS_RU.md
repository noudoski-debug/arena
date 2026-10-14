# EncoreDLC v3 — с системой ключей и Telegram-продажами

Внутри:
- **cheat/** (папка `jni/`, build.sh) — сам чит EncoreDLC v3 с фиксами (Silent Aim, ESP, Watermark, nicknames).
- **encore-server/** — бэкенд Node.js:
  - REST API для проверки HWID-ключей (`/api/verify`, `/api/key/:key/public`, `/api/admin/*`)
  - Telegram-бот продаж (@BotFather + @CryptoBot) с автоматической выдачей ключей после оплаты TON/USDT
  - Веб-сайт (лендинг), пользовательская панель `/panel`, админка `/admin`
  - БД SQLite (локально) или Postgres (Render/Neon бесплатно)

---

## 1. Бесплатный деплой на Render.com

### 1.1 Создаём Telegram-бота
1. В Telegram пишем [@BotFather](https://t.me/BotFather), `/newbot` → получаем **BOT_TOKEN**.
2. Пишем [@CryptoBot](https://t.me/CryptoBot), **Crypto Pay → Create App** → получаем **CRYPTOBOT_TOKEN**.
3. Узнаём свой Telegram user_id у [@userinfobot](https://t.me/userinfobot) — это будет админ.

### 1.2 Создаём сервис на Render
1. Регистрируйся на [render.com](https://render.com) (есть бесплатный план).
2. **New → Web Service**, подключи свой GitHub-репозитор (создай приватный и залей эту папку `encore-server/`).
3. Заполни:
   - **Root Directory:** `encore-server`
   - **Build Command:** `npm install`
   - **Start Command:** `node src/index.js`
   - **Runtime:** Node 18+.
4. В разделе **Environment** добавь переменные (скопируй из `.env.example`):

| Ключ | Значение |
|---|---|
| `BOT_TOKEN` | токен от @BotFather |
| `ADMIN_IDS` | твой Telegram id (например `123456789`) |
| `PUBLIC_CHANNEL` | опционально: @канал для обязательной подписки |
| `SUPPORT_USERNAME` | твой юзернейм поддержки |
| `CRYPTOBOT_TOKEN` | токен от @CryptoBot Crypto Pay |
| `PRICE_7D` | `3` |
| `PRICE_30D` | `9` |
| `PRICE_LIFETIME` | `29` |
| `ADMIN_USER` | `admin` |
| `ADMIN_PASS` | **сложный пароль** от веб-админки |
| `PUBLIC_BASE_URL` | https://твой-сервис.onrender.com |

5. Нажми **Create Web Service**. Render соберёт и запустит сервер. На бесплатном плане
   диск эфемерный — при перезапуске БД (SQLite) обнулится. Для постоянной базы:
   - Создай **PostgreSQL** на Render (встроенная бесплатная на 90 дней) или на
     [Neon](https://neon.tech/)/[Supabase](https://supabase.com/) бесплатно навсегда,
     положи его URL в `DATABASE_URL` (postgres://…).
   - **Важно:** в этой сборке драйвер Postgres не включён для лёгкости; SQLite
     работает из коробки на платном диске (1$). Для бесплатного старта хватит SQLite
     — раз в несколько дней ключи могут сброситься, но пользователь просто введёт
     старый заново (HWID сбросится тоже). Для продакшена поставь платный диск или Neon Postgres.

### 1.3 Устанавливаем вебхук бота
После первого деплоя открой в браузере `https://твой-сервис.onrender.com/` и проверь, что сайт открывается. Бот автоматически заработает через long-polling (вебхук не нужен).

---

## 2. Альтернативные бесплатные хосты
- **Railway.app** — бесплатно 5$/мес (с надёжным Postgres). Заливается через `railway up`.
- **fly.io** — 3 бесплатные ВПС-машины с постоянным диском.
- **Koyeb.com** — есть бесплатный план.
- VPS (от 2$) — самый надёжный вариант: обычный Ubuntu, `npm install && node src/index.js` + pm2, SQLite-файл на диске.

---

## 3. Встройка бэкенда в чит
По умолчанию чит ходит на URL, который прописан в `jni/src/Auth.cpp` в переменной `g_url`.
Замени её на свой домен перед сборкой:
```cpp
static std::string g_url = "https://твой-сервис.onrender.com";
```
Или после запуска через cmd.txt на девайсе:
```
echo "auth_url http://192.168.1.100:3000" > /data/local/tmp/67/cmd.txt
```

### Системные требования на девайсе для HTTPS
На рутовом Android обычно есть `/system/bin/curl` или toybox-`wget`, через них чит ходит по HTTPS без встраивания TLS-стека. Если у тебя кастом без них — ходим по обычному HTTP на своём домене (через nginx reverse proxy или без TLS), либо встрой mbedtls вручную.

---

## 4. Как пользоваться

1. Открой бота в Telegram → `/start` → «🛒 Купить» → выбери тариф → оплати TON/USDT через CryptoBot.
2. После оплаты бот автоматически пришлёт ключ вида `ENCR-XXXX-XXXX-XXXX`.
3. Запусти чит на телефоне, открой меню (F10/Ins на клавиатуре или тап по вотермарке), в разделе **Settings** введи ключ → **OK**.
4. Ключ привяжется к HWID автоматически, читы активируются.
5. Веб-админка `/admin` (basic auth по ADMIN_USER/PASS): статистика, выдача/бан/сброс HWID ключей.

### Важно
- Пользователь не сможет использовать один ключ на двух устройствах (HWID check).
- Сброс HWID — только через админку или Telegram-команду `/reset ENCR-XXXX-XXXX-XXXX`.
- Если у юзера кратковременно нет сети — чит остаётся рабочим 2+ минуты, чтобы не дропать его во время игры.
