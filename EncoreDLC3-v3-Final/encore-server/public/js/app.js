// Lightweight helper: sets Telegram links if bot info configured.
(function(){
  // По умолчанию ведём на /panel (там инструкция и форма покупки).
  // Админ позже вставит из .env ссылку на бота в переменную окна через серверный шаблон,
  // но в дефолтной статике оставим /panel.
  document.querySelectorAll('a[href="#"]').forEach(a => a.setAttribute('href','/panel'));
})();
