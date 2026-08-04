// api.js — fetch wrapper: returns an envelope {ok,status,data,code,message}.
// The `http` verbs resolve to the payload on success and throw an Error
// carrying .code/.status/.envelope on failure; banner hooks live here too.
import { t } from './i18n.js';

// Low-level fetch. Never throws for HTTP errors — always resolves to an envelope:
//   ok      — 2xx and the body did not itself report ok:false
//   status  — HTTP status code
//   data    — parsed JSON body (null for 204 No Content)
//   code    — machine error code from data.error.code ("" on success)
//   message — human error message from data.error.message ("" on success)
// Only a transport-level failure (server unreachable) throws, with err.network.
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
  // 204 No Content: nothing to parse, and the empty body is not an error.
  if (res.status === 204) {
    return { ok: true, status: 204, data: null, code: '', message: '' };
  }
  let data = null;
  try { data = await res.json(); } catch (_) { /* non-JSON */ }

  const ok = res.ok && !(data && data.ok === false);
  let code = '';
  let message = '';
  if (!ok) {
    const err = data && data.error;
    if (err && typeof err === 'object') {
      code = err.code || '';
      message = err.message || '';
    } else if (err) {
      message = String(err);
    }
    // 405 Method Not Allowed: surface the Allow header as a hint.
    if (res.status === 405) {
      const allow = res.headers.get('Allow');
      if (allow) message = (message ? `${message} ` : '') + `[Allow: ${allow}]`;
    }
    if (!message) message = `HTTP ${res.status}`;
  }
  return { ok, status: res.status, data, code, message };
}

// Convenience verbs: resolve to the payload (data) on success so views keep
// `const s = await http.get(...); s.lang`. On failure they throw an Error with
// .code/.status/.envelope attached for rich rendering (showError(code, message)).
async function unwrap(pr) {
  const r = await pr;
  if (r.ok) return r.data;
  const e = new Error(r.message);
  e.code = r.code;
  e.status = r.status;
  e.envelope = r;
  throw e;
}

export const http = {
  get:   (p) => unwrap(api('GET', p)),
  post:  (p, b) => unwrap(api('POST', p, b)),
  patch: (p, b) => unwrap(api('PATCH', p, b)),
  put:   (p, b) => unwrap(api('PUT', p, b)),
  del:   (p) => unwrap(api('DELETE', p)),
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
// Displays "code: message" when a code is present, else just the message.
// Single-arg calls (message-only) keep working.
export function showError(code, message) {
  const b = document.getElementById('banner');
  b.textContent = message !== undefined
    ? (code ? `${code}: ${message}` : message)
    : String(code);
  b.classList.remove('hidden');
}
export function clearError() {
  document.getElementById('banner').classList.add('hidden');
}
