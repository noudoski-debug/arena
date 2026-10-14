// api.js — REST эндпоинты чита и админки.
//
// Публичные:
//   GET  /api/status           — статус сервиса (версия)
//   POST /api/verify           — проверка ключа читом
//   GET  /api/key/:key/public  — публичный статус ключа (для веб-панельки пользователя)
//
// Админские (Basic Auth ADMIN_USER:ADMIN_PASS):
//   GET  /api/admin/stats
//   GET  /api/admin/keys?filter=all|active|unused|expired|banned
//   POST /api/admin/issue      { plan, count }
//   POST /api/admin/ban        { key, reason }
//   POST /api/admin/unban      { key }
//   POST /api/admin/reset      { key }
//   POST /api/admin/delete     { key }
//   GET  /api/admin/checks
const express = require('express');
const keys = require('./keys');

function isAdminReq(req) {
  const u = process.env.ADMIN_USER || 'admin';
  const p = process.env.ADMIN_PASS || 'encoreadmin';
  const h = req.headers.authorization || '';
  const m = h.match(/^Basic (.+)$/);
  if (!m) return false;
  const [user, pass] = Buffer.from(m[1], 'base64').toString().split(':');
  return user === u && pass === p;
}

function requireAdmin(req, res, next) {
  if (!isAdminReq(req)) {
    res.set('WWW-Authenticate', 'Basic realm="Encore Admin"');
    return res.status(401).send('Auth required');
  }
  next();
}

const router = express.Router();

router.get('/status', (req, res) => {
  res.json({ ok:true, name:'EncoreDLC', version:'3.0.0', time: Date.now() });
});

// Проверка ключа читом.
router.post('/verify', express.json(), (req, res) => {
  const { key, hwid, version } = req.body || {};
  if (!key || !hwid) return res.status(400).json({ ok:false, reason:'bad_request' });
  if (hwid.length < 8 || hwid.length > 128) return res.json({ ok:false, reason:'bad_hwid' });
  const ip = (req.headers['x-forwarded-for'] || req.socket.remoteAddress || '').toString().split(',')[0].trim();
  const r = keys.verifyKey(String(key).trim(), String(hwid).trim(), String(version || '3.0'), ip);
  res.json(r);
});

router.get('/key/:key/public', (req, res) => {
  const k = keys.normalizeKey(req.params.key);
  const row = require('./db').prepare(`SELECT key,plan,hwid IS NOT NULL AS bound,banned,expires_at,activated_at FROM keys WHERE key=?`).get(k);
  if (!row) return res.status(404).json({ ok:false });
  res.json({
    ok: !row.banned && (row.plan === 'life' || !row.expires_at || Date.now() < row.expires_at),
    plan: row.plan,
    bound: !!row.bound,
    banned: !!row.banned,
    expires_at: row.expires_at
  });
});

// ---- admin ----
router.get('/admin/stats', requireAdmin, (req, res) => res.json(keys.stats()));

router.get('/admin/keys', requireAdmin, (req, res) => {
  res.json(keys.listKeys({ filter: req.query.filter || 'all', limit: parseInt(req.query.limit || '200') }));
});

router.get('/admin/checks', requireAdmin, (req, res) => res.json(keys.recentChecks(parseInt(req.query.limit||'100'))));

router.post('/admin/issue', requireAdmin, express.json(), (req, res) => {
  const { plan, count = 1 } = req.body || {};
  if (!keys.PLANS[plan] || count < 1 || count > 100) return res.status(400).json({ ok:false, error:'bad plan or count' });
  const issued = [];
  for (let i = 0; i < count; i++) issued.push(keys.issueKey(plan, 0));
  issued.forEach(k => keys.markPaid(k, plan));
  res.json({ ok:true, keys: issued });
});

router.post('/admin/ban', requireAdmin, express.json(), (req, res) => {
  if (!req.body.key) return res.status(400).json({ ok:false });
  keys.banKey(keys.normalizeKey(req.body.key), req.body.reason || '');
  res.json({ ok:true });
});
router.post('/admin/unban', requireAdmin, express.json(), (req, res) => {
  keys.unbanKey(keys.normalizeKey(req.body.key)); res.json({ ok:true });
});
router.post('/admin/reset', requireAdmin, express.json(), (req, res) => {
  keys.resetKey(keys.normalizeKey(req.body.key)); res.json({ ok:true });
});
router.post('/admin/delete', requireAdmin, express.json(), (req, res) => {
  keys.deleteKey(keys.normalizeKey(req.body.key)); res.json({ ok:true });
});

module.exports = router;
