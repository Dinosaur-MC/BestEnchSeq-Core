// algorithms.js — Algorithms view: list loaded algorithms with per-row detail
// (GET /api/algorithms/{name}) and plugin unload (POST /api/algorithms/unload
// {name}), plus load a plugin dir.
import { http, showError, clearError, esc } from '../api.js';
import { t } from '../i18n.js';

// Algorithm names are URL path segments; encode anything unsafe there while
// keeping the RFC 3986 unreserved set (same helper as the other views).
const URL_SAFE_RE = /^[A-Za-z0-9\-._~:]+$/;
function encSeg(s) {
  let out = '';
  for (const ch of String(s)) out += URL_SAFE_RE.test(ch) ? ch : encodeURIComponent(ch);
  return out;
}

export async function render(el) {
  const myView = el.dataset.view; // bail if route() re-stamps ownership mid-await
  let names;
  try {
    names = await http.get('/api/algorithms');
  } catch (e) {
    showError(e.message);
    return;
  }
  if (el.dataset.view !== myView) return;
  // Per-row detail (small list; N+1 is fine) — origin decides the unload
  // affordance (builtin strategies are never unloadable).
  const rows = await Promise.all(names.map(async (n) => {
    let d = null;
    try { d = await http.get(`/api/algorithms/${encSeg(n)}`); } catch (_) { /* list-only */ }
    return { name: n, d };
  }));
  if (el.dataset.view !== myView) return;

  el.innerHTML = `<h2>${t('alg.title')}</h2><div class="card">
    <table><thead><tr><th>${t('alg.name')}</th><th>${t('alg.origin')}</th><th></th></tr></thead><tbody>` +
    rows.map((r) => `<tr>
        <td>${esc(r.name)}</td>
        <td>${r.d ? esc(r.d.origin === 'plugin' ? t('alg.plugin') : t('alg.builtin')) : ''}</td>
        <td>
          <button data-detail="${esc(r.name)}">${t('alg.detail')}</button>
          ${r.d && r.d.origin === 'plugin'
            ? `<button class="secondary" data-unload="${esc(r.name)}">${t('alg.unload')}</button>` : ''}
        </td></tr>`).join('') +
    `</tbody></table>
     <div id="alg-detail"></div>
     <label>${t('alg.load_dir')}</label><input class="dir">
     <button class="load">${t('alg.load')}</button>
     <div id="alg-msg" class="mono"></div></div>`;

  // Detail expander: toggles a meta table with every field the backend
  // detail endpoint provides ({name, version, origin, plugin_path,
  // is_resumable, supported_mode, has_audit}).
  const detailPanel = document.getElementById('alg-detail');
  el.querySelectorAll('[data-detail]').forEach((b) => b.addEventListener('click', async () => {
    clearError();
    const name = b.dataset.detail;
    if (detailPanel.dataset.name === name) { detailPanel.innerHTML = ''; delete detailPanel.dataset.name; return; }
    try {
      const d = await http.get(`/api/algorithms/${encSeg(name)}`);
      if (el.dataset.view !== myView) return;
      detailPanel.dataset.name = name;
      detailPanel.innerHTML = `<h4>${esc(name)}</h4><table class="meta-table">
        <tr><th>${t('alg.origin')}</th><td>${esc(d.origin === 'plugin' ? t('alg.plugin') : t('alg.builtin'))}</td></tr>
        <tr><th>${t('alg.version')}</th><td>${esc(d.version || '')}</td></tr>
        <tr><th>${t('alg.mode')}</th><td>${esc(d.supported_mode || '')}</td></tr>
        <tr><th>${t('alg.resumable')}</th><td>${d.is_resumable ? t('status.solve_yes') : t('status.solve_no')}</td></tr>
        <tr><th>${t('alg.audit')}</th><td>${d.has_audit ? t('status.solve_yes') : t('status.solve_no')}</td></tr>
        <tr><th>${t('alg.plugin_path')}</th><td class="mono">${esc(d.plugin_path || '')}</td></tr>
      </table>`;
    } catch (e) { showError(e.message); }
  }));

  // Unload (plugins only — the button is hidden for builtin origins; a race
  // with an active solve surfaces as the backend's 409 TASK_ACTIVE).
  el.querySelectorAll('[data-unload]').forEach((b) => b.addEventListener('click', async () => {
    clearError();
    const name = b.dataset.unload;
    try {
      await http.post('/api/algorithms/unload', { name });
      if (el.dataset.view !== myView) return;
      detailPanel.innerHTML = '';
      render(el);
    } catch (e) { showError(e.message); }
  }));

  el.querySelector('.load').addEventListener('click', async () => {
    const dir = el.querySelector('.dir').value.trim();
    if (!dir) return;
    clearError();
    try {
      const r = await http.post('/api/algorithms/load', { dir });
      if (el.dataset.view !== myView) return; // user navigated away mid-POST
      // Success is neutral status, not an error — write it inline, not the red banner.
      document.getElementById('alg-msg').textContent = `${t('alg.loaded')}: ${r.loaded}`;
      render(el);
    } catch (e) { showError(e.message); }
  });
}
