// settings.js — Settings view: edit language + log level, persist via PUT.
import { http, showError, clearError } from '../api.js';
import { t, setLang, applyI18n } from '../i18n.js';

export async function render(el) {
  const myView = el.dataset.view; // bail if route() re-stamps ownership mid-await
  const s = await http.get('/api/settings');
  if (el.dataset.view !== myView) return;
  el.innerHTML = `<h2>${t('set.title')}</h2><div class="card">
    <label>${t('set.lang')}</label>
    <select id="set-lang">
      <option value="en_US" ${s.lang === 'en_US' ? 'selected' : ''}>English</option>
      <option value="zh_CN" ${s.lang === 'zh_CN' ? 'selected' : ''}>中文</option>
    </select>
    <label>${t('set.log_level')}</label>
    <select id="set-loglevel">
      <option value="0" ${s.log_level === 0 ? 'selected' : ''}>Debug</option>
      <option value="1" ${s.log_level === 1 ? 'selected' : ''}>Info</option>
      <option value="2" ${s.log_level === 2 ? 'selected' : ''}>Warn</option>
      <option value="3" ${s.log_level === 3 ? 'selected' : ''}>Error</option>
    </select>
    <div style="margin-top:12px"><button id="set-save">${t('set.save')}</button></div>
    <div id="set-msg"></div></div>`;
  document.getElementById('set-save').addEventListener('click', async () => {
    clearError();
    try {
      const body = {
        lang: document.getElementById('set-lang').value,
        log_level: parseInt(document.getElementById('set-loglevel').value, 10),
      };
      await http.put('/api/settings', body);
      if (el.dataset.view !== myView) return; // save finished after navigation
      setLang(body.lang === 'zh_CN' ? 'zh-CN' : 'en-US');
      applyI18n();
      document.getElementById('set-msg').textContent = t('set.saved');
    } catch (e) { showError(e.message); }
  });
}
