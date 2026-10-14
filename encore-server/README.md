# EncoreDLC v3 — Key server + Telegram sales bot

Node.js backend for EncoreDLC cheat key system.

## Local run (for testing)

```bash
cd encore-server
cp .env.example .env
# fill .env: BOT_TOKEN, ADMIN_IDS, CRYPTOBOT_TOKEN, ADMIN_PASS, PUBLIC_BASE_URL
npm install
npm start
```

Open http://localhost:3000 — landing page
http://localhost:3000/panel — user key panel
http://localhost:3000/admin — admin dashboard (basic auth: admin / encoreadmin)

Key verify endpoint used by the cheat:
```
POST /api/verify
{ "key": "ENCR-XXXX-XXXX-XXXX", "hwid": "abc123", "version": "3.0" }
```

## Free deploy

See `../README_SERVER_RU.md` (Russian) for Render.com / Railway / VPS setup.
