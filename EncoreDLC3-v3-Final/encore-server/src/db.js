// db.js — хранилище данных.
// Приоритет 1: better-sqlite3 (нативная, быстрая, если удалось установить).
// Приоритет 2: встроенный JSON-файл (всегда работает, без нативных зависимостей,
// идеально для бесплатных хостов и Render).
// При DATABASE_URL=postgres — в проде на Render/Neon можно подключить pg-модуль,
// но бесплатный старт работает и на JSON-бэкенде — для маленького чита этого с головой.
const fs = require('fs');
const path = require('path');

let driver = 'json'; // 'json' | 'sqlite'
let sqlDb = null;

// ---- Пробуем SQLite ----
try {
  const Database = require('better-sqlite3');
  const dir = path.join(__dirname, '..', 'data');
  fs.mkdirSync(dir, { recursive: true });
  sqlDb = new Database(path.join(dir, 'encore.db'));
  sqlDb.pragma('journal_mode = WAL');
  sqlDb.pragma('foreign_keys = ON');
  driver = 'sqlite';
  console.log('[db] using better-sqlite3');
} catch (e) {
  console.log('[db] better-sqlite3 not available, using JSON storage:', e.message);
}

// ---- JSON storage (fallback) ----
const DATA_DIR = path.join(__dirname, '..', 'data');
const DATA_FILE = path.join(DATA_DIR, 'encore.json');
fs.mkdirSync(DATA_DIR, { recursive: true });

function loadJson() {
  try { return JSON.parse(fs.readFileSync(DATA_FILE, 'utf8')); }
  catch(e) {
    return {
      users: {},            // id -> user row
      keys: {},             // key -> key row
      orders: {},           // id -> order row
      checks: [],           // array of last N
      _seq: 0
    };
  }
}
function saveJson(db) {
  // Чекам держим только последние 1000
  if (db.checks.length > 1000) db.checks = db.checks.slice(-1000);
  fs.writeFileSync(DATA_FILE, JSON.stringify(db, null, 2));
}

// ---- Schema (sqlite) ----
const SCHEMA = `
CREATE TABLE IF NOT EXISTS users (
  id           INTEGER PRIMARY KEY,
  username     TEXT, first_name TEXT, created_at INTEGER, banned INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS keys (
  key          TEXT PRIMARY KEY,
  plan         TEXT NOT NULL, created_by INTEGER, created_at INTEGER NOT NULL,
  hwid         TEXT, hwid_bound_at INTEGER, activated_at INTEGER, expires_at INTEGER,
  banned       INTEGER NOT NULL DEFAULT 0, ban_reason TEXT
);
CREATE TABLE IF NOT EXISTS orders (
  id           TEXT PRIMARY KEY, tg_user INTEGER NOT NULL, plan TEXT NOT NULL,
  amount_usd   REAL NOT NULL, status TEXT NOT NULL DEFAULT 'pending',
  crypto_invoice_id INTEGER, pay_url TEXT,
  created_at   INTEGER NOT NULL, paid_at INTEGER, issued_key TEXT
);
CREATE TABLE IF NOT EXISTS checks (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  key          TEXT NOT NULL, hwid TEXT NOT NULL, ok INTEGER NOT NULL,
  version      TEXT, ip TEXT, ts INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_keys_hwid ON keys(hwid);
CREATE INDEX IF NOT EXISTS idx_checks_key ON checks(key);
CREATE INDEX IF NOT EXISTS idx_orders_user ON orders(tg_user,status);
`;

if (driver === 'sqlite') {
  sqlDb.exec(SCHEMA);
}

// ---- Unified API ----
// prepare returns object with .get(...), .all(...), .run(...) — duck-typed.

function prepare(sql) {
  if (driver === 'sqlite') return sqlDb.prepare(sql);
  // JSON backend: реализуем только те шаблоны, которые реально использует код.
  // Писать полный SQL парсер избыточно для маленького сервера.
  return jsonStmt(sql);
}

function exec(sql) {
  if (driver === 'sqlite') sqlDb.exec(sql);
}

function getLastInsertRowid() {
  if (driver === 'sqlite') return Number(sqlDb.lastInsertRowid || 0);
  // для JSON insert мы сами проставляем id
  return 0;
}

// ---- JSON statement runner ----
function jsonStmt(sql) {
  const s = sql.trim().replace(/\s+/g,' ');
  // Простые сопоставления по шаблонам из keys.js/api.js/bot.js
  return {
    get(...params) { return jsonQuery(s, params, true); },
    all(...params) { return jsonQuery(s, params, false); },
    run(...params) { return jsonRun(s, params); }
  };
}

function jsonQuery(sql, params, one) {
  const db = loadJson();
  if (/SELECT 1 FROM users WHERE id\s*=\s*\?/.test(sql)) {
    const r = db.users[String(params[0])];
    return r ? { '1': 1 } : undefined;
  }
  if (/SELECT 1 FROM keys WHERE key\s*=\s*\?/.test(sql)) {
    const r = db.keys[params[0]];
    return r ? { '1': 1 } : undefined;
  }
  if (/SELECT \* FROM keys WHERE key\s*=\s*\?/.test(sql)) {
    return db.keys[params[0]];
  }
  if (/SELECT COUNT\(\*\) c FROM keys/.test(sql)) {
    const flt = keysFilter(sql);
    let c = 0;
    for (const k of Object.values(db.keys)) if (matchKeyFilter(k, flt)) c++;
    return { c };
  }
  if (/SELECT COUNT\(\*\) c FROM users/.test(sql)) { return { c: Object.keys(db.users).length }; }
  if (/SELECT COUNT\(\*\) c FROM orders WHERE status='pending'/.test(sql)) {
    let c = 0; for (const o of Object.values(db.orders)) if (o.status==='pending') c++;
    return { c };
  }
  if (/SELECT COUNT\(\*\) c FROM checks WHERE ts > \?/.test(sql)) {
    const cutoff = params[0]; let c=0; for(const ch of db.checks) if(ch.ts>cutoff) c++; return {c};
  }
  if (/SELECT .+ FROM keys WHERE created_by=\?/i.test(sql)) {
    return Object.values(db.keys).filter(k=>k.created_by===params[0]).slice(0,20);
  }
  if (/SELECT .+ FROM orders WHERE status='pending'/.test(sql)) {
    return Object.values(db.orders).filter(o=>o.status==='pending');
  }
  if (/SELECT \* FROM orders WHERE id=\?/.test(sql)) {
    return db.orders[params[0]];
  }
  if (/FROM keys .*ORDER BY created_at DESC LIMIT \?/.test(sql) ||
      /FROM keys ORDER BY created_at DESC LIMIT \?/.test(sql)) {
    const flt = keysFilter(sql);
    const lim = params[params.length-1]|0;
    const rows = Object.values(db.keys).filter(k=>matchKeyFilter(k,flt))
      .sort((a,b)=>(b.created_at||0)-(a.created_at||0)).slice(0,lim);
    return one?rows[0]:rows;
  }
  if (/FROM keys WHERE banned=0 AND expires_at IS NOT NULL AND expires_at <=/i.test(sql) && /ORDER BY expires_at DESC/.test(sql)) {
    const lim = params[params.length-1]|0;
    const now = Date.now();
    return Object.values(db.keys).filter(k=>!k.banned && k.expires_at && k.expires_at<=now)
      .sort((a,b)=>(b.expires_at||0)-(a.expires_at||0)).slice(0,lim);
  }
  if (/FROM checks ORDER BY ts DESC LIMIT \?/.test(sql)) {
    const lim = params[params.length-1]|0;
    return [...db.checks].sort((a,b)=>b.ts-a.ts).slice(0,lim);
  }
  // join orders<->keys (bot mykeys)
  if (/SELECT k\.\* FROM orders o JOIN keys k ON k\.key=o\.issued_key WHERE o\.tg_user=\?/.test(sql)) {
    const uid = params[0];
    const out = [];
    for (const o of Object.values(db.orders)) {
      if (o.tg_user!==uid) continue;
      if (o.issued_key && db.keys[o.issued_key]) out.push(db.keys[o.issued_key]);
    }
    return out;
  }
  return one ? undefined : [];
}

function keysFilter(sql) {
  if (/hwid IS NULL AND banned=0/.test(sql)) return 'unused';
  if (/banned=0 AND .*expires_at.*> \?/.test(sql) || /hwid IS NOT NULL AND banned=0/.test(sql)) return 'active';
  if (/WHERE banned=0 AND expires_at IS NOT NULL AND expires_at <=/.test(sql)) return 'expired';
  if (/WHERE banned=1/.test(sql)) return 'banned';
  return 'all';
}
function matchKeyFilter(k, f){
  if (f==='unused') return !k.hwid && !k.banned;
  if (f==='active') return k.hwid && !k.banned && (k.plan==='life' || !k.expires_at || k.expires_at>Date.now());
  if (f==='expired') return !k.banned && k.expires_at && k.expires_at<=Date.now();
  if (f==='banned') return !!k.banned;
  return true;
}

function jsonRun(sql, params) {
  const db = loadJson();
  let changed = false;

  // INSERT users
  if (/INSERT INTO users/.test(sql)) {
    const [id, username, first_name, created_at] = params;
    db.users[String(id)] = { id:id|0, username, first_name, created_at:created_at|0, banned:0 };
    changed = true;
  }
  // UPDATE users SET username=?, first_name=? WHERE id=?
  else if (/UPDATE users SET username=\?/.test(sql)) {
    const [username, first_name, id] = params;
    if (db.users[String(id)]) { db.users[String(id)].username = username; db.users[String(id)].first_name = first_name; changed = true; }
  }
  // INSERT keys
  else if (/INSERT INTO keys \(key,plan/.test(sql)) {
    const [key,plan,created_by,created_at] = params;
    db.keys[key] = { key, plan, created_by, created_at:created_at|0, banned:0 };
    changed = true;
  }
  // UPDATE keys SET expires_at=? WHERE key=?
  else if (/UPDATE keys SET expires_at=/.test(sql)) {
    const [exp, key] = params;
    if (db.keys[key]) { db.keys[key].expires_at = exp; changed = true; }
  }
  // UPDATE keys SET hwid=?, hwid_bound_at=?, activated_at=? WHERE key=?
  else if (/UPDATE keys SET hwid=\?/.test(sql)) {
    const [hwid, hbat, aat, key] = params;
    if (db.keys[key]) { Object.assign(db.keys[key], {hwid:hwid||null, hwid_bound_at:hbat||null, activated_at:aat||null}); changed=true; }
  }
  // UPDATE keys SET banned=1, ban_reason=? WHERE key=?
  else if (/banned=1/.test(sql)) {
    const [reason, key] = params;
    if (db.keys[key]) { db.keys[key].banned = 1; db.keys[key].ban_reason = reason||''; changed = true; }
  }
  // UPDATE keys SET banned=0, ban_reason=NULL
  else if (/banned=0/.test(sql) && /ban_reason=NULL/.test(sql)) {
    const key = params[0]; if (db.keys[key]) { db.keys[key].banned=0; db.keys[key].ban_reason=null; changed=true; }
  }
  // UPDATE keys SET hwid=NULL, hwid_bound_at=NULL, activated_at=NULL
  else if (/hwid=NULL/.test(sql)) {
    const key = params[0]; if (db.keys[key]) { db.keys[key].hwid=null; db.keys[key].hwid_bound_at=null; db.keys[key].activated_at=null; changed=true; }
  }
  // DELETE FROM keys WHERE key=?
  else if (/DELETE FROM keys/.test(sql)) {
    delete db.keys[params[0]]; changed = true;
  }
  // INSERT orders
  else if (/INSERT INTO orders \(id,tg_user,plan,amount_usd,created_at\)/.test(sql)) {
    const [id,tg_user,plan,amount_usd,created_at] = params;
    db.orders[id] = { id, tg_user:tg_user|0, plan, amount_usd:+amount_usd, status:'pending', created_at:created_at|0 };
    changed = true;
  }
  // UPDATE orders SET crypto_invoice_id=?, pay_url=? WHERE id=?
  else if (/UPDATE orders SET crypto_invoice_id=/.test(sql) && /issued_key/.test(sql) === false) {
    const [inv, url, id] = params;
    if (db.orders[id]) { db.orders[id].crypto_invoice_id = inv; db.orders[id].pay_url = url; changed=true; }
  }
  // UPDATE orders SET status='paid', paid_at=?, issued_key=? WHERE id=?
  else if (/status='paid'/.test(sql)) {
    const [paid_at, key, id] = params;
    if (db.orders[id]) { db.orders[id].status='paid'; db.orders[id].paid_at=paid_at|0; db.orders[id].issued_key=key; changed=true; }
  }
  // INSERT checks
  else if (/INSERT INTO checks/.test(sql)) {
    const [key, hwid, ok, version, ip, ts] = params;
    db._seq = (db._seq||0)+1;
    db.checks.push({id:db._seq,key,hwid,ok:ok?1:0,version:version||'',ip:ip||'',ts:ts|0});
    changed = true;
  }
  if (changed) saveJson(db);
  return { changes: changed?1:0 };
}

module.exports = { prepare, exec, getLastInsertRowid, driver };
