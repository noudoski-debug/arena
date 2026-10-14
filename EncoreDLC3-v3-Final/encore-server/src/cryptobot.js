// cryptobot.js — интеграция с @CryptoBot (Crypto Pay API).
// Доки: https://help.crypt.bot/crypto-pay-api
// Поддерживает TON, USDT, BTC, ETH, LTC, BUSD.
const https = require('https');

const API_HOST = 'pay.crypt.bot';

function apiCall(token, method, params = {}) {
  return new Promise((resolve, reject) => {
    const body = new URLSearchParams(params).toString();
    const opts = {
      host: API_HOST,
      path: `/api/${method}`,
      method: 'POST',
      headers: {
        'Crypto-Pay-API-Token': token,
        'Content-Type': 'application/x-www-form-urlencoded',
        'Content-Length': Buffer.byteLength(body)
      }
    };
    const req = https.request(opts, res => {
      let data = '';
      res.on('data', c => data += c);
      res.on('end', () => {
        try {
          const j = JSON.parse(data);
          if (j.ok) resolve(j.result);
          else reject(new Error(`CryptoBot ${j.error?.error_code}: ${j.error?.error || data}`));
        } catch (e) { reject(new Error('bad json: ' + data)); }
      });
    });
    req.on('error', reject);
    req.write(body);
    req.end();
  });
}

// Создать счёт. asset можно 'USDT'/'TON'/'BTC'/...; fiat-расчёт проще через
// currency_type='fiat' и фиат 'USD', но работает не во всех странах, поэтому
// в дефолте берём TON и считаем сумму в долларах по курсу CryptoBot.
async function createInvoice({ token, amountUsd, description = 'EncoreDLC', payload }) {
  // Для простоты используем TON как основную монету (низкие комиссии, популярна в СНГ).
  // Если хочешь USDT — поменяй asset и убери getRates конвертацию.
  const rates = await apiCall(token, 'getExchangeRates');
  const tonRate = rates.find(r => r.source === 'TON' && r.target === 'USD');
  if (!tonRate) throw new Error('TON/USD rate not found');
  const amountTon = (amountUsd / parseFloat(tonRate.rate)).toFixed(4);

  const inv = await apiCall(token, 'createInvoice', {
    asset: 'TON',
    amount: amountTon,
    description,
    paid_btn_name: 'callback',
    paid_btn_url: process.env.PUBLIC_BASE_URL + '/panel',
    payload: payload ? JSON.stringify(payload) : '',
    allow_comments: 'false',
    allow_anonymous: 'false',
    expires_in: '3600' // 1 час на оплату
  });
  return {
    id: inv.invoice_id,
    pay_url: inv.pay_url,
    amount_ton: inv.amount,
    asset: inv.asset,
    status: inv.status
  };
}

// Скажет какие счета оплачены с момента последнего вызова (по invoice_id).
async function getInvoices(token, ids = []) {
  if (!ids.length) return [];
  const res = await apiCall(token, 'getInvoices', { invoice_ids: ids.join(',') });
  return res.items || [];
}

module.exports = { createInvoice, getInvoices, apiCall };
