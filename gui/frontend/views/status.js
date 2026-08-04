// status.js — Status view: active profile, algorithm count, solve state, uptime.
import { http, esc } from '../api.js';
import { t } from '../i18n.js';

export async function render(el) {
  const s = await http.get('/api/status');
  const h = await http.get('/health');
  el.innerHTML = `<h2>${t('status.title')}</h2><div class="card">
    <table><tbody>
      <tr><td>${t('status.profile')}</td><td>${esc(s.active_profile)}</td></tr>
      <tr><td>${t('status.algorithms')}</td><td>${s.algorithm_count}</td></tr>
      <tr><td>${t('status.solve')}</td><td>${s.has_active_solve}</td></tr>
      <tr><td>${t('status.uptime')}</td><td>${s.uptime_ms} / ${h.uptime_ms}</td></tr>
    </tbody></table></div>`;
}
