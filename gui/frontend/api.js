// api.js — fetch wrapper: {ok:false,error} → thrown Error, error banner hooks.
import { t } from './i18n.js';

export async function api(method, path, body) {
  const opts = { method, headers: {} };
  if (body !== undefined) {
    opts.headers['Content-Type'] = 'application/json';
    opts.body = JSON.stringify(body);
  }
  let res;
  try {
    res = await fetch(path, opts);
  } catch (_) {
    throw new Error(t('err.network'));
  }
  let data = null;
  try { data = await res.json(); } catch (_) { /* non-JSON */ }
  if (!res.ok || (data && data.ok === false)) {
    throw new Error((data && data.error) ? data.error : `HTTP ${res.status}`);
  }
  return data;
}

export const http = {
  get:  (p) => api('GET', p),
  post: (p, b) => api('POST', p, b),
  put:  (p, b) => api('PUT', p, b),
  del:  (p) => api('DELETE', p),
};

// Escape a backend-supplied string for safe interpolation into innerHTML.
// Shared here (not per-view) so every view uses the same escaping.
export function esc(s) {
  return String(s).replace(/[&<>"']/g,
    (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

// Global error banner. Lives here (not app.js) so views don't create a
// circular module dependency: app.js imports views, views import api.js,
// api.js imports nothing.
export function showError(msg) {
  const b = document.getElementById('banner');
  b.textContent = msg;
  b.classList.remove('hidden');
}
export function clearError() {
  document.getElementById('banner').classList.add('hidden');
}
