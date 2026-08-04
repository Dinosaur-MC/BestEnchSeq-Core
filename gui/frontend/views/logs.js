// logs.js — Logs view: poll the ring-buffer log body once per second.
import { http, showError } from '../api.js';
import { t } from '../i18n.js';

export async function render(el) {
  const myView = el.dataset.view; // bail if route() re-stamps ownership mid-await
  el.innerHTML = `<h2>${t('logs.title')}</h2><div class="card"><pre class="mono" id="log-body"></pre></div>`;
  let timer = null;
  const refresh = async () => {
    const body = document.getElementById('log-body');
    if (!body || !document.body.contains(body)) { clearInterval(timer); return; }
    try {
      const d = await http.get('/api/logs');
      if (el.dataset.view !== myView) return; // poll resumed after navigation
      body.textContent = (d.logs || []).map((l) => `[${l.level}] ${l.message}`).join('\n');
    } catch (e) { showError(e.message); }
  };
  refresh();
  timer = setInterval(refresh, 1000);
  window.addEventListener('hashchange', () => clearInterval(timer), { once: true });
}
