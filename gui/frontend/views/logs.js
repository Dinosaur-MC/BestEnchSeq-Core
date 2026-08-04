// logs.js — Logs view: incremental tail via GET /api/logs?since=&limit=200
// (ring-buffer seq cursor returned as `next`), plus an optional live tail over
// EventSource('/api/logs/events'). The poller is the reliable path; SSE just
// smooths delivery. A per-line dedup set keeps SSE and the poller from
// double-printing the same line.
import { http, showError } from '../api.js';
import { t } from '../i18n.js';

export async function render(el) {
  const myView = el.dataset.view; // bail if route() re-stamps ownership mid-await
  el.innerHTML = `<h2>${t('logs.title')}</h2><div class="card"><pre class="mono" id="log-body"></pre></div>`;

  let timer = null;
  let since = 0;    // last-seen ring seq; 0 = "everything", replaced by `next`
  let first = true; // first fetch backfills the whole visible history
  const seen = new Set();

  const body = () => document.getElementById('log-body');
  const appendLogs = (logs) => {
    const b = body();
    if (!b || !document.body.contains(b)) return;
    let lines = '';
    for (const l of logs) {
      // Ring entries may or may not carry a seq yet (pre-controller builds);
      // fall back to (timestamp,message) so the dedup still works either way.
      const key = l.seq !== undefined ? `s${l.seq}` : `${l.timestamp_ms}:${l.message}`;
      if (seen.has(key)) continue;
      seen.add(key);
      lines += (lines ? '\n' : '') + `[${l.level}] ${l.message}`;
    }
    if (lines) b.textContent = b.textContent ? `${b.textContent}\n${lines}` : lines;
  };

  const refresh = async () => {
    const b = body();
    if (!b || !document.body.contains(b)) { clearInterval(timer); return; }
    try {
      const d = await http.get(`/api/logs?since=${since}&limit=200`);
      if (el.dataset.view !== myView) return; // poll resumed after navigation
      if (first) { b.textContent = ''; seen.clear(); }
      appendLogs(d.logs || []);
      first = false;
      if (d.next !== undefined) since = d.next;
    } catch (e) { showError(e.message); }
  };

  refresh();
  timer = setInterval(refresh, 1000);
  window.addEventListener('hashchange', () => clearInterval(timer), { once: true });

  // Optional live tail. On failure the stream is dropped and the poller covers
  // the gap — no reconnect loop (a non-SSE-capable proxy would just spin).
  if (typeof EventSource !== 'undefined') {
    try {
      const es = new EventSource('/api/logs/events');
      es.onmessage = (ev) => {
        const b = body();
        if (!b || !document.body.contains(b)) { es.close(); return; }
        try {
          const data = JSON.parse(ev.data);
          if (data && Array.isArray(data.logs)) appendLogs(data.logs);
          else if (data && data.level) appendLogs([data]);
        } catch (_) { /* non-JSON frame — ignore */ }
      };
      es.onerror = () => es.close(); // poller is the fallback
    } catch (_) { /* EventSource unavailable → polling only */ }
  }
}
