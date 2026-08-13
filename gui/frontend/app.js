import { setLang, applyI18n } from './i18n.js';
import { http, showError, clearError } from './api.js';
import * as calculator from './views/calculator.js';
import * as profiles from './views/profiles.js';
import * as algorithms from './views/algorithms.js';
import * as history from './views/history.js';
import * as settings from './views/settings.js';
import * as status from './views/status.js';

const views = { calculator, profiles, algorithms, history, settings, status };

// showError/clearError live in api.js — imported above (no module cycle).

async function boot() {
  // Sync UI language from the backend.
  try {
    const s = await http.get('/api/settings');
    setLang(s.lang === 'zh_CN' ? 'zh-CN' : 'en-US');
  } catch (_) { /* default en-US */ }
  applyI18n();
  route();
}

function route() {
  const hash = (location.hash || '#/calculator').replace(/^#\//, '');
  const [raw, ...rest] = hash.split('/');
  const name = raw || 'calculator';
  const view = views[name] || views.calculator;
  document.querySelectorAll('.nav-item').forEach((a) =>
    a.classList.toggle('active', a.dataset.route === name));
  const el = document.getElementById('view');
  el.innerHTML = '';
  el.dataset.view = name; // stamp ownership so async renders can bail on navigation
  clearError();
  if (view.render) view.render(el, rest);
}

window.addEventListener('hashchange', route);
window.addEventListener('DOMContentLoaded', boot);
