// algorithms.js — Algorithms view: list loaded algorithms and load a plugin dir.
import { http, showError, esc } from '../api.js';
import { t } from '../i18n.js';

export async function render(el) {
  const data = await http.get('/api/algorithm');
  el.innerHTML = `<h2>${t('alg.title')}</h2><div class="card"><table><tbody>` +
    data.algorithms.map((a) => `<tr><td>${esc(a.name)}</td><td>${esc(a.mode || '')}</td></tr>`).join('') +
    `</tbody></table>
     <label>${t('alg.load_dir')}</label><input class="dir">
     <button class="load">${t('alg.load')}</button></div>`;
  el.querySelector('.load').addEventListener('click', async () => {
    const dir = el.querySelector('.dir').value.trim();
    if (!dir) return;
    try { const r = await http.post('/api/algorithm/load', { dir }); showError(`${t('alg.loaded')}: ${r.loaded}`); }
    catch (e) { showError(e.message); }
  });
}
