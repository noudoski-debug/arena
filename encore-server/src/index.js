// index.js — entry point: Express + static сайт + API + Telegram-бот.
require('dotenv').config();
const path = require('path');
const express = require('express');

const api = require('./api');
const { startBot, startPolling } = require('./bot');

const app = express();
app.disable('x-powered-by');
app.set('trust proxy', true);

// Логи всех запросов
app.use((req, res, next) => {
  res.on('finish', () => {
    if (req.path.startsWith('/api/admin') || req.path.startsWith('/api/verify'))
      console.log(`[http] ${req.method} ${req.path} ${res.statusCode} ${(req.headers['x-forwarded-for']||req.socket.remoteAddress||'').split(',')[0].trim()}`);
  });
  next();
});

app.use('/api', api);

// Публичные страницы: landing, panel (пользователь), admin
app.use(express.static(path.join(__dirname, '..', 'public'), { extensions: ['html'] }));
app.get('/', (req, res) => res.sendFile(path.join(__dirname, '..', 'public', 'index.html')));
app.get('/panel', (req, res) => res.sendFile(path.join(__dirname, '..', 'public', 'panel.html')));
app.get('/admin', (req, res) => res.sendFile(path.join(__dirname, '..', 'public', 'admin', 'index.html')));

const PORT = parseInt(process.env.PORT || '3000', 10);
app.listen(PORT, '0.0.0.0', async () => {
  console.log(`[server] EncoreDLC v3 server listening on ${PORT}`);
  console.log(`[server] public URL: ${process.env.PUBLIC_BASE_URL || 'http://localhost:' + PORT}`);
  try {
    await startBot();
    startPolling();
  } catch (e) {
    console.error('[server] bot start failed:', e.message);
  }
});

// Корректное завершение
process.once('SIGINT', () => process.exit(0));
process.once('SIGTERM', () => process.exit(0));
