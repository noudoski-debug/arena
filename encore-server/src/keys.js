// keys.js — генерация, активация, проверка HWID-ключей.
//
// Ключ вида  ENCR-XXXX-XXXX-XXXX  (всего 12 символов + префикс).
// Символы — крипто-безопасный набор без похожих (0/O, 1/I/L).
const crypto = require('crypto');
const { prepare } = require('./db');

const KEY_PREFIX = 'ENCR';
const KEY_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';

const PLANS = {
  '7d':   { label: '7 дней',        days: 7 },
  '30d':  { label: '30 дней',       days: 30 },
  'life': { label: 'Навсегда',      days: null }
};

function genSegment(len = 4) {
  let s = '';
  const chars = KEY_ALPHABET;
  for (let i = 0; i < len; i++) {
    s += chars[crypto.randomInt(chars.length)];
  }
  return s;
}

function generateKey() {
  return `${KEY_PREFIX}-${genSegment(4)}-${genSegment(4)}-${genSegment(4)}`;
}

function normalizeKey(raw) {
  if (!raw) return '';
  return String(raw).toUpperCase().replace(/[^A-Z0-9]/g, '').replace(/^ENCR/, 'ENCR-').replace(/(\w{4})(?=\w)/g, '$1-').replace(/-$/, '');
}

// Создать новую неактивированную подписку (для ручной выдачи админом или после оплаты).
function issueKey(plan, createdBy = 0) {
  if (!PLANS[plan]) throw new Error('bad plan');
  let tries = 0;
  while (tries++ < 20) {
    const k = generateKey();
    const exists = prepare('SELECT 1 FROM keys WHERE key = ?').get(k);
    if (exists) continue;
    prepare(`INSERT INTO keys (key,plan,created_by,created_at) VALUES (?,?,?,?)`)
      .run(k, plan, createdBy, Date.now());
    return k;
  }
  throw new Error('keygen failed');
}

// Проверка ключа читом.  Возвращает {ok, reason?, plan?, expires?}
// Если ключ не привязан — привязывает к hwid. Если привязан — проверяет совпадение.
function verifyKey(keyRaw, hwid, version = '3.0', ip = '') {
  const key = normalizeKey(keyRaw);
  const now = Date.now();
  const ins = prepare(`INSERT INTO checks (key,hwid,ok,version,ip,ts) VALUES (?,?,?,?,?,?)`);

  const row = prepare(`SELECT * FROM keys WHERE key = ?`).get(key);
  if (!row) { ins.run(key, hwid || '', 0, version, ip, now); return { ok:false, reason:'invalid_key' }; }
  if (row.banned) { ins.run(key, hwid || '', 0, version, ip, now); return { ok:false, reason:'banned' }; }

  if (!row.hwid) {
    // Первая активация — привязать.
    prepare(`UPDATE keys SET hwid=?, hwid_bound_at=?, activated_at=? WHERE key=?`)
      .run(hwid, now, now, key);
  } else if (row.hwid !== hwid) {
    ins.run(key, hwid || '', 0, version, ip, now);
    return { ok:false, reason:'hwid_mismatch' };
  }

  if (row.plan !== 'life' && row.expires_at && now > row.expires_at) {
    ins.run(key, hwid || '', 0, version, ip, now);
    return { ok:false, reason:'expired' };
  }
  ins.run(key, hwid || '', 1, version, ip, now);

  return {
    ok: true,
    plan: row.plan,
    activated_at: row.activated_at || now,
    expires_at: row.plan === 'life' ? null : (row.expires_at || (() => {
      const d = PLANS[row.plan].days;
      const exp = (row.activated_at || now) + d * 86400000;
      prepare(`UPDATE keys SET expires_at=? WHERE key=?`).run(exp, key);
      return exp;
    })())
  };
}

function activateKey(keyRaw, hwid) {
  // То же что verifyKey но только первый раз. Используется читом для ручного
  // ввода при входе.
  return verifyKey(keyRaw, hwid);
}

// После успешной оплаты — посчитать expires и записать его.
function markPaid(key, plan) {
  const now = Date.now();
  const exp = PLANS[plan].days ? now + PLANS[plan].days * 86400000 : null;
  if (exp) prepare(`UPDATE keys SET expires_at=? WHERE key=?`).run(exp, key);
  return exp;
}

function listKeys({ filter = 'all', limit = 100 } = {}) {
  let sql = `SELECT * FROM keys ORDER BY created_at DESC LIMIT ?`;
  const params = [limit];
  if (filter === 'unused') sql = `SELECT * FROM keys WHERE hwid IS NULL AND banned=0 ORDER BY created_at DESC LIMIT ?`;
  else if (filter === 'active') sql = `SELECT * FROM keys WHERE hwid IS NOT NULL AND banned=0 AND (expires_at IS NULL OR expires_at > ${Date.now()}) ORDER BY created_at DESC LIMIT ?`;
  else if (filter === 'expired') sql = `SELECT * FROM keys WHERE banned=0 AND expires_at IS NOT NULL AND expires_at <= ${Date.now()} ORDER BY expires_at DESC LIMIT ?`;
  else if (filter === 'banned') sql = `SELECT * FROM keys WHERE banned=1 ORDER BY created_at DESC LIMIT ?`;
  return prepare(sql).all(...params);
}

function stats() {
  const now = Date.now();
  return {
    total:   prepare(`SELECT COUNT(*) c FROM keys`).get().c,
    unused:  prepare(`SELECT COUNT(*) c FROM keys WHERE hwid IS NULL AND banned=0`).get().c,
    active:  prepare(`SELECT COUNT(*) c FROM keys WHERE hwid IS NOT NULL AND banned=0 AND (expires_at IS NULL OR expires_at > ?)`).get(now).c,
    expired: prepare(`SELECT COUNT(*) c FROM keys WHERE banned=0 AND expires_at IS NOT NULL AND expires_at <= ?`).get(now).c,
    banned:  prepare(`SELECT COUNT(*) c FROM keys WHERE banned=1`).get().c,
    users:   prepare(`SELECT COUNT(*) c FROM users`).get().c,
    orders_pending: prepare(`SELECT COUNT(*) c FROM orders WHERE status='pending'`).get().c,
    checks_today: prepare(`SELECT COUNT(*) c FROM checks WHERE ts > ?`).get(now - 86400000).c
  };
}

function banKey(key, reason = '') {
  prepare(`UPDATE keys SET banned=1, ban_reason=? WHERE key=?`).run(reason, key);
}
function unbanKey(key) {
  prepare(`UPDATE keys SET banned=0, ban_reason=NULL WHERE key=?`).run(key);
}
function resetKey(key) {
  prepare(`UPDATE keys SET hwid=NULL, hwid_bound_at=NULL, activated_at=NULL WHERE key=?`).run(key);
}
function deleteKey(key) {
  prepare(`DELETE FROM keys WHERE key=?`).run(key);
}

function recentChecks(limit = 50) {
  return prepare(`SELECT * FROM checks ORDER BY ts DESC LIMIT ?`).all(limit);
}

module.exports = {
  PLANS, generateKey, normalizeKey, issueKey, verifyKey, activateKey,
  markPaid, listKeys, stats, banKey, unbanKey, resetKey, deleteKey,
  recentChecks
};
