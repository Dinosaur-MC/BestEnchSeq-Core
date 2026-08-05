// settings.js — Settings view: edit language + log level, persist via PATCH.
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
      <option value="0" ${s.log_level === 0 ? 'selected' : ''}>${t('level.debug')}</option>
      <option value="1" ${s.log_level === 1 ? 'selected' : ''}>${t('level.info')}</option>
      <option value="2" ${s.log_level === 2 ? 'selected' : ''}>${t('level.warn')}</option>
      <option value="3" ${s.log_level === 3 ? 'selected' : ''}>${t('level.error')}</option>
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
      await http.patch('/api/settings', body);
      if (el.dataset.view !== myView) return; // save finished after navigation
      setLang(body.lang === 'zh_CN' ? 'zh-CN' : 'en-US');
      applyI18n();
      // Re-render the current view so its t() strings pick up the new language.
      // (No "Saved" note: the synchronous re-render clears #view, so it would never paint.)
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    } catch (e) { showError(e.code, e.message); }
  });
}
