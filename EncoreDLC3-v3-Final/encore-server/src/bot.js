// bot.js — Telegram-бот продажи ключей EncoreDLC.
//  /start           — приветствие + меню
//  "Купить"         — выбор тарифа → ссылка на CryptoBot счёт
//  "Мои ключи"      — активные ключи, срок
//  "Активировать"   — ввод ключа (если покупка не через бота)
//  "Поддержка"      — ссылка на саппорт
// Админ-команды: /panel (inline-кнопка в админку веб), /issue <план> <ник>,
//                /ban, /unban, /reset, /stats
const { Telegraf, Markup } = require('telegraf');
const { prepare } = require('./db');
const keys = require('./keys');
const cb = require('./cryptobot');

const ADMINS = (process.env.ADMIN_IDS || '')
  .split(',').map(s => parseInt(s.trim(), 10)).filter(Boolean);
const CHANNEL = (process.env.PUBLIC_CHANNEL || '').replace('@', '');
const SUPPORT = process.env.SUPPORT_USERNAME || '';
const PRICES = {
  '7d':   parseFloat(process.env.PRICE_7D       || '3'),
  '30d':  parseFloat(process.env.PRICE_30D      || '9'),
  'life': parseFloat(process.env.PRICE_LIFETIME || '29')
};

function upsertUser(from) {
  const ex = prepare(`SELECT 1 FROM users WHERE id=?`).get(from.id);
  if (!ex) {
    prepare(`INSERT INTO users (id,username,first_name,created_at) VALUES (?,?,?,?)`)
      .run(from.id, from.username || '', from.first_name || '', Date.now());
  } else {
    prepare(`UPDATE users SET username=?, first_name=? WHERE id=?`)
      .run(from.username || '', from.first_name || '', from.id);
  }
}

function isAdmin(id) { return ADMINS.includes(id); }

async function requireJoin(ctx, next) {
  if (!CHANNEL) return next();
  try {
    const m = await ctx.telegram.getChatMember('@' + CHANNEL, ctx.from.id);
    if (['member','administrator','creator'].includes(m.status)) return next();
    await ctx.reply(
      `📢 Чтобы пользоваться ботом, подпишись на канал @${CHANNEL}`,
      Markup.inlineKeyboard([[Markup.button.url('Перейти на канал', `https://t.me/${CHANNEL}`)],[Markup.button.callback('✅ Я подписался', 'check_join')]])
    );
  } catch (e) { return next(); } // если бот не админ канала — не блокируем
}

function mainMenu() {
  return Markup.inlineKeyboard([
    [Markup.button.callback('🛒 Купить',       'buy')],
    [Markup.button.callback('🔑 Мои ключи',    'mykeys')],
    [Markup.button.callback('ℹ️  Инструкция',  'help')],
    ...(SUPPORT ? [[Markup.button.url('💬 Поддержка', 'https://t.me/' + SUPPORT.replace('@',''))]] : [])
  ]);
}

function buyMenu() {
  return Markup.inlineKeyboard([
    [Markup.button.callback(`7 дней  — $${PRICES['7d']}`,   'plan_7d')],
    [Markup.button.callback(`30 дней — $${PRICES['30d']}`, 'plan_30d')],
    [Markup.button.callback(`Навсегда — $${PRICES['life']}`, 'plan_life')],
    [Markup.button.callback('⬅️ Назад', 'main')]
  ]);
}

async function sendMain(ctx) {
  await ctx.reply(
    '⚡ <b>EncoreDLC</b> — внешний чит для Oxide Survival Island\n\n' +
    'Выбери действие в меню ниже:',
    { parse_mode: 'HTML', ...mainMenu(), disable_web_page_preview: true }
  );
}

function formatKey(k) {
  const planLabel = keys.PLANS[k.plan]?.label || k.plan;
  let status = '✅ Активен';
  if (k.banned) status = '🚫 Забанен';
  else if (k.plan !== 'life' && k.expires_at && Date.now() > k.expires_at) status = '⌛ Истёк';
  else if (!k.hwid) status = '⏳ Не активирован';
  let exp = '';
  if (k.plan === 'life') exp = 'навсегда';
  else if (k.expires_at) exp = `до ${new Date(k.expires_at).toLocaleDateString('ru-RU')}`;
  return `<code>${k.key}</code>\n• Тариф: <b>${planLabel}</b>\n• Статус: ${status}${exp ? '\n• ' + exp : ''}`;
}

let bot;

async function startBot() {
  if (!process.env.BOT_TOKEN || process.env.BOT_TOKEN.includes('example')) {
    console.log('[bot] BOT_TOKEN not configured — bot disabled.');
    return;
  }
  bot = new Telegraf(process.env.BOT_TOKEN);

  bot.use(async (ctx, next) => {
    if (ctx.from) upsertUser(ctx.from);
    return next();
  });

  bot.start(requireJoin, sendMain);
  bot.command('start', requireJoin, sendMain);

  bot.action('main', async ctx => { try { await ctx.deleteMessage(); } catch(e){} return sendMain(ctx); });
  bot.action('check_join', requireJoin, async ctx => ctx.answerCbQuery('Спасибо! Можно пользоваться ✅').then(()=>sendMain(ctx)));
  bot.action('buy',   ctx => ctx.editMessageText('🛒 <b>Выбери тариф:</b>', { parse_mode:'HTML', ...buyMenu() }));
  bot.action('help',  ctx => ctx.editMessageText(
    'ℹ️ <b>Инструкция:</b>\n\n' +
    '1) Купи ключ в разделе «🛒 Купить».\n' +
    '2) После оплаты бот пришлёт тебе ключ вида <code>ENCR-XXXX-XXXX-XXXX</code>.\n' +
    '3) Запусти чит, вставь ключ в поле при первом запуске.\n' +
    '4) Ключ привяжется к твоему устройству автоматически.\n\n' +
    '⚠️ Сброс HWID — только через поддержку (1 раз бесплатно).',
    { parse_mode:'HTML', ...Markup.inlineKeyboard([[Markup.button.callback('⬅️ Назад','main')]]) }));

  bot.action('mykeys', async ctx => {
    const rows = prepare(`SELECT * FROM keys WHERE created_by=? OR hwid IN (SELECT hwid FROM keys WHERE created_by=? AND hwid IS NOT NULL) ORDER BY created_at DESC LIMIT 20`).all(ctx.from.id, ctx.from.id);
    // Также ищем ключи по заказам этого пользователя (issued_key в orders)
    const orderKeys = prepare(`SELECT k.* FROM orders o JOIN keys k ON k.key=o.issued_key WHERE o.tg_user=? ORDER BY o.created_at DESC LIMIT 20`).all(ctx.from.id);
    const seen = new Set();
    const list = [];
    for (const k of [...rows, ...orderKeys]) {
      if (!k || seen.has(k.key)) continue;
      seen.add(k.key); list.push(k);
    }
    if (!list.length) {
      return ctx.editMessageText('🔑 У тебя пока нет ключей. Купи первый в разделе «🛒 Купить».',
        Markup.inlineKeyboard([[Markup.button.callback('🛒 Купить','buy')],[Markup.button.callback('⬅️ Назад','main')]]));
    }
    const text = '🔑 <b>Твои ключи:</b>\n\n' + list.map(formatKey).join('\n\n');
    return ctx.editMessageText(text, { parse_mode:'HTML', ...Markup.inlineKeyboard([[Markup.button.callback('⬅️ Назад','main')]]) });
  });

  // buy plan
  for (const plan of Object.keys(PRICES)) {
    bot.action('plan_' + plan, async ctx => {
      const price = PRICES[plan];
      if (!process.env.CRYPTOBOT_TOKEN || process.env.CRYPTOBOT_TOKEN.includes('example')) {
        // Оплата не настроена — админ может вручную выдавать через /issue.
        return ctx.editMessageText(
          '⚠️ Оплата через CryptoBot пока не настроена админом.\n' +
          'Напиши в поддержку для ручной покупки.',
          Markup.inlineKeyboard([[Markup.button.callback('⬅️ Назад','buy')]]));
      }
      try {
        const internalId = 'tg_' + ctx.from.id + '_' + plan + '_' + Date.now();
        prepare(`INSERT INTO orders (id,tg_user,plan,amount_usd,created_at) VALUES (?,?,?,?,?)`)
          .run(internalId, ctx.from.id, plan, price, Date.now());
        const inv = await cb.createInvoice({
          token: process.env.CRYPTOBOT_TOKEN,
          amountUsd: price,
          description: `EncoreDLC — ${keys.PLANS[plan].label}`,
          payload: { order: internalId }
        });
        prepare(`UPDATE orders SET crypto_invoice_id=?, pay_url=? WHERE id=?`)
          .run(inv.id, inv.pay_url, internalId);
        await ctx.editMessageText(
          `💸 <b>${keys.PLANS[plan].label}</b> — $${price}\n\n` +
          `Оплата в TON (≈ ${inv.amount_ton} TON).\n` +
          `Счёт действует 1 час. После оплаты ключ придёт автоматически.`,
          { parse_mode:'HTML', ...Markup.inlineKeyboard([
            [Markup.button.url('💎 Оплатить', inv.pay_url)],
            [Markup.button.callback('🔄 Проверить оплату', 'checkpay_'+internalId)],
            [Markup.button.callback('⬅️ Назад','buy')]
          ]) });
      } catch (e) {
        console.error('[bot] create invoice failed', e);
        await ctx.editMessageText('❌ Ошибка создания счёта. Напиши в поддержку.',
          Markup.inlineKeyboard([[Markup.button.callback('⬅️ Назад','buy')]]));
      }
    });
  }

  bot.action(/^checkpay_(.+)$/, async ctx => {
    const id = ctx.match[1];
    const o = prepare(`SELECT * FROM orders WHERE id=?`).get(id);
    if (!o) return ctx.answerCbQuery('Счёт не найден');
    if (o.status === 'paid') {
      await ctx.answerCbQuery('✅ Оплата найдена!');
      return ctx.editMessageText(`✅ Оплата прошла!\nТвой ключ: <code>${o.issued_key}</code>`, { parse_mode:'HTML', ...mainMenu() });
    }
    await ctx.answerCbQuery('⌛ Оплата ещё не получена. Попробуй через 10 секунд.');
  });

  // ---- Админ ----
  bot.command('panel', ctx => {
    if (!isAdmin(ctx.from.id)) return;
    return ctx.reply('⚙️ Панель админа:',
      Markup.inlineKeyboard([[Markup.button.url('🌐 Веб-админка', process.env.PUBLIC_BASE_URL + '/admin')]]));
  });
  bot.command('stats', ctx => {
    if (!isAdmin(ctx.from.id)) return;
    const s = keys.stats();
    return ctx.reply(`📊 Статистика:\nВсего ключей: ${s.total}\nНеиспольз.: ${s.unused}\nАктивных: ${s.active}\nИстёкших: ${s.expired}\nЗабанен.: ${s.banned}\nЮзеров: ${s.users}\nПроверок сегодня: ${s.checks_today}`);
  });
  bot.command('issue', ctx => {
    if (!isAdmin(ctx.from.id)) return;
    const args = (ctx.message.text || '').trim().split(/\s+/);
    const plan = args[1];
    const count = parseInt(args[2] || '1', 10);
    if (!keys.PLANS[plan] || count < 1 || count > 50)
      return ctx.reply('Использование: /issue <7d|30d|life> [кол-во]\nПример: /issue 30d 5');
    const issued = [];
    for (let i = 0; i < count; i++) issued.push(keys.issueKey(plan, ctx.from.id));
    return ctx.reply(`✅ Выдано ${count} ключей (${keys.PLANS[plan].label}):\n` +
      issued.map(k => `<code>${k}</code>`).join('\n'), { parse_mode:'HTML' });
  });
  bot.command('ban', ctx => {
    if (!isAdmin(ctx.from.id)) return;
    const key = keys.normalizeKey((ctx.message.text||'').split(/\s+/)[1] || '');
    const k = prepare(`SELECT 1 FROM keys WHERE key=?`).get(key);
    if (!k) return ctx.reply('Ключ не найден. Использование: /ban ENCR-XXXX-XXXX-XXXX [причина]');
    const reason = (ctx.message.text||'').split(/\s+/).slice(2).join(' ');
    keys.banKey(key, reason);
    return ctx.reply('🚫 Ключ забанен');
  });
  bot.command('unban', ctx => {
    if (!isAdmin(ctx.from.id)) return;
    const key = keys.normalizeKey((ctx.message.text||'').split(/\s+/)[1] || '');
    keys.unbanKey(key); return ctx.reply('✅ Разбанен');
  });
  bot.command('reset', ctx => {
    if (!isAdmin(ctx.from.id)) return;
    const key = keys.normalizeKey((ctx.message.text||'').split(/\s+/)[1] || '');
    keys.resetKey(key); return ctx.reply('🔄 HWID сброшен');
  });

  // webhook/polling
  await bot.launch();
  console.log(`[bot] started as @${(await bot.telegram.getMe()).username}`);
  return bot;
}

// Периодическая проверка новых оплат (на случай если webhook не настроен).
async function pollPaidOrders() {
  if (!process.env.CRYPTOBOT_TOKEN || process.env.CRYPTOBOT_TOKEN.includes('example')) return;
  const pending = prepare(`SELECT * FROM orders WHERE status='pending'`).all();
  if (!pending.length) return;
  const ids = pending.map(o => o.crypto_invoice_id).filter(Boolean);
  if (!ids.length) return;
  try {
    const invs = await cb.getInvoices(process.env.CRYPTOBOT_TOKEN, ids);
    for (const o of pending) {
      const inv = invs.find(i => i.invoice_id === o.crypto_invoice_id);
      if (!inv || inv.status !== 'paid') continue;
      // Выдать ключ
      const k = keys.issueKey(o.plan, o.tg_user);
      keys.markPaid(k, o.plan);
      prepare(`UPDATE orders SET status='paid', paid_at=?, issued_key=? WHERE id=?`)
        .run(Date.now(), k, o.id);
      if (bot) {
        await bot.telegram.sendMessage(o.tg_user,
          `✅ <b>Оплата получена!</b>\n\nТвой ключ: <code>${k}</code>\nТариф: ${keys.PLANS[o.plan].label}\nСохрани его и введи при запуске чита.`,
          { parse_mode:'HTML', ...mainMenu() }).catch(()=>{});
      }
    }
  } catch (e) { console.error('[bot] poll paid failed', e.message); }
}

function startPolling() {
  setInterval(pollPaidOrders, 15000);
}

module.exports = { startBot, startPolling };
