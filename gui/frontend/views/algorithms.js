// algorithms.js — Algorithms view: list loaded algorithms and load a plugin dir.
import { http, showError, clearError, esc } from '../api.js';
import { t } from '../i18n.js';

export async function render(el) {
  const myView = el.dataset.view; // bail if route() re-stamps ownership mid-await
  const data = await http.get('/api/algorithms');
  if (el.dataset.view !== myView) return;
  el.innerHTML = `<h2>${t('alg.title')}</h2><div class="card"><table><tbody>` +
    data.map((a) => `<tr><td>${esc(a)}</td></tr>`).join('') +
    `</tbody></table>
     <label>${t('alg.load_dir')}</label><input class="dir">
     <button class="load">${t('alg.load')}</button>
     <div id="alg-msg" class="mono"></div></div>`;
  el.querySelector('.load').addEventListener('click', async () => {
    const dir = el.querySelector('.dir').value.trim();
    if (!dir) return;
    clearError();
    try {
      const r = await http.post('/api/algorithms/load', { dir });
      if (el.dataset.view !== myView) return; // user navigated away mid-POST
      // Success is neutral status, not an error — write it inline, not the red banner.
      document.getElementById('alg-msg').textContent = `${t('alg.loaded')}: ${r.loaded}`;
    } catch (e) { showError(e.message); }
  });
}
