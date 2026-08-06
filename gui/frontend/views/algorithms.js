// algorithms.js — Algorithms view: list loaded algorithms with version /
// supported-mode / origin shown inline, a per-entry expandable detail row
// (GET /api/algorithms/{name} fields) and plugin unload (POST
// /api/algorithms/unload {name}), plus plugin-dir loading with a directory
// picker (GET /api/fs/list?path=).
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

// supported_mode wire values ("direct" / "inventory" / "both") → labels.
const MODE_KEYS = { direct: 'alg.mode_direct', inventory: 'alg.mode_inventory', both: 'alg.mode_both' };
function modeLabel(m) { return (m && MODE_KEYS[m]) ? t(MODE_KEYS[m]) : (m || ''); }

// The full detail meta table (every AlgorithmDetail field) — rendered into
// the inline expansion row directly under the algorithm's entry.
function detailHtml(d) {
  return `<table class="meta-table">
    <tr><th>${t('alg.origin')}</th><td>${esc(d.origin === 'plugin' ? t('alg.plugin') : t('alg.builtin'))}</td></tr>
    <tr><th>${t('alg.version')}</th><td>${esc(d.version || '')}</td></tr>
    <tr><th>${t('alg.mode')}</th><td>${esc(modeLabel(d.supported_mode))}</td></tr>
    <tr><th>${t('alg.resumable')}</th><td>${d.is_resumable ? t('status.solve_yes') : t('status.solve_no')}</td></tr>
    <tr><th>${t('alg.audit')}</th><td>${d.has_audit ? t('status.solve_yes') : t('status.solve_no')}</td></tr>
    <tr><th>${t('alg.plugin_path')}</th><td class="mono">${esc(d.plugin_path || '')}</td></tr>
  </table>`;
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
  // affordance (builtin strategies are never unloadable), version/mode feed
  // the inline columns, and the cached payload feeds the expansion row.
  const rows = await Promise.all(names.map(async (n) => {
    let d = null;
    try { d = await http.get(`/api/algorithms/${encSeg(n)}`); } catch (_) { /* list-only */ }
    return { name: n, d };
  }));
  if (el.dataset.view !== myView) return;

  el.innerHTML = `<h2>${t('alg.title')}</h2><div class="card">
    <table><thead><tr><th>${t('alg.name')}</th><th>${t('alg.version')}</th><th>${t('alg.mode')}</th><th>${t('alg.origin')}</th><th></th></tr></thead><tbody>` +
    rows.map((r) => `<tr>
        <td>${esc(r.name)}</td>
        <td>${r.d ? esc(r.d.version || '') : ''}</td>
        <td>${r.d ? esc(modeLabel(r.d.supported_mode)) : ''}</td>
        <td>${r.d ? esc(r.d.origin === 'plugin' ? t('alg.plugin') : t('alg.builtin')) : ''}</td>
        <td>
          <button data-detail="${esc(r.name)}">${t('alg.detail')}</button>
          ${r.d && r.d.origin === 'plugin'
            ? `<button class="secondary" data-unload="${esc(r.name)}">${t('alg.unload')}</button>` : ''}
        </td></tr>
        <tr class="alg-detail-row" data-detailrow="${esc(r.name)}" hidden>
          <td colspan="5">${r.d ? detailHtml(r.d) : ''}</td>
        </tr>`).join('') +
    `</tbody></table>
     <label>${t('alg.load_dir')}</label><input class="dir">
     <button class="load">${t('alg.load')}</button>
     <div id="alg-msg" class="mono"></div></div>`;

  // Detail expander: toggles the inline row right under the entry (the row
  // was already populated at render time — no refetch on toggle).
  el.querySelectorAll('[data-detail]').forEach((b) => b.addEventListener('click', () => {
    clearError();
    const name = b.dataset.detail;
    let row = null;
    el.querySelectorAll('[data-detailrow]').forEach((r) => { if (r.dataset.detailrow === name) row = r; });
    if (row) row.hidden = !row.hidden;
  }));

  // Unload (plugins only — the button is hidden for builtin origins; a race
  // with an active solve surfaces as the backend's 409 TASK_ACTIVE).
  el.querySelectorAll('[data-unload]').forEach((b) => b.addEventListener('click', async () => {
    clearError();
    const name = b.dataset.unload;
    try {
      await http.post('/api/algorithms/unload', { name });
      if (el.dataset.view !== myView) return;
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
