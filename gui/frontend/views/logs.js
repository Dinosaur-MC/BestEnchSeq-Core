// logs.js — Logs view: live tail over SSE /api/logs/events (primary), with a
// 2s polling fallback (/api/logs?since=&limit=200) when the stream drops.
// The poller also backfills history on entry and after pause/resume. A per-line
// dedup set (seq, else timestamp+message) keeps both sources from double-
// printing; the level filter is a pure view over an in-memory session buffer.
import { http, showError } from '../api.js';
import { esc } from '../api.js';
import { t } from '../i18n.js';

// Level ordering used by the filter (data `level` is lowercase).
const LVL_RANK = { debug: 0, info: 1, warn: 2, error: 3 };
const pad2 = (n) => String(n).padStart(2, '0');
const pad3 = (n) => String(n).padStart(3, '0');
const fmtTs = (ms) => {
  const d = new Date(ms);
  return `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}.${pad3(d.getMilliseconds())}`;
};

export async function render(el) {
  const myView = el.dataset.view; // bail if route() re-stamps ownership mid-await
  el.innerHTML = `<h2>${t('logs.title')}</h2><div class="card">
    <div class="log-bar">
      <label for="log-level">${t('logs.level')}</label>
      <select id="log-level">
        <option value="debug">${t('level.debug')}</option>
        <option value="info">${t('level.info')}</option>
        <option value="warn">${t('level.warn')}</option>
        <option value="error">${t('level.error')}</option>
      </select>
      <button id="log-pause">${t('logs.pause')}</button>
      <button class="secondary" id="log-clear">${t('logs.clear')}</button>
      <span class="log-status" id="log-status"></span>
    </div>
    <div class="mono" id="log-body"></div></div>`;

  const levelSel = document.getElementById('log-level');
  const pauseBtn = document.getElementById('log-pause');
  const clearBtn = document.getElementById('log-clear');
  const statusEl = document.getElementById('log-status');
  const body = () => document.getElementById('log-body');

  let es = null;          // EventSource (null when closed/unavailable)
  let timer = null;       // poll fallback interval
  let retry = null;       // SSE re-open timeout
  let paused = false;
  let since = 0;          // last-seen ring seq (== timestamp_ms); 0 = "everything"
  let minLv = 'debug';    // filter: only show levels >= this
  const seen = new Set();
  const lines = [];       // session buffer (all received lines, unfiltered)

  const setStatus = (key) => { statusEl.textContent = t(key); };

  const lineHtml = (l) =>
    `<div class="log-line log-${l.level}">[${fmtTs(l.ts)}] [${l.level}] ${esc(l.msg)}</div>`;

  // Re-render the whole body from the session buffer (level filter change).
  const renderAll = () => {
    const b = body();
    if (!b || !document.body.contains(b)) return;
    const shown = lines.filter((l) => LVL_RANK[l.level] >= LVL_RANK[minLv]);
    b.innerHTML = shown.map(lineHtml).join('');
  };

  const appendLogs = (logs) => {
    const b = body();
    if (!b || !document.body.contains(b)) return;
    let html = '';
    for (const l of logs || []) {
      // Ring entries may or may not carry a seq yet (pre-controller builds);
      // fall back to (timestamp,message) so the dedup still works either way.
      const level = l.level !== undefined && LVL_RANK[l.level] !== undefined ? l.level : 'info';
      const key = l.seq !== undefined ? `s${l.seq}` : `${l.timestamp_ms}:${l.message}`;
      if (seen.has(key)) continue;
      seen.add(key);
      if (l.timestamp_ms !== undefined && l.timestamp_ms > since) since = l.timestamp_ms;
      const rec = { level, ts: l.timestamp_ms, msg: String(l.message || '') };
      lines.push(rec);
      if (LVL_RANK[level] >= LVL_RANK[minLv]) html += lineHtml(rec);
    }
    if (html) b.innerHTML += html;
  };

  const cleanup = () => { closeSSE(); stopPolling(); stopRetry(); };
  const stopPolling = () => { if (timer) { clearInterval(timer); timer = null; } };
  const stopRetry = () => { if (retry) { clearTimeout(retry); retry = null; } };
  const closeSSE = () => {
    if (es) { es.onerror = null; es.onmessage = null; es.onopen = null; es.close(); es = null; }
  };

  // Fallback path: poll the ring tail until SSE comes back (or forever if the
  // server/proxy has no SSE support). `since` cursor + dedup make it overlap
  // safely with any live SSE frames.
  const startPolling = () => {
    if (timer || paused) return;
    setStatus('logs.polling');
    timer = setInterval(backfill, 2000);
  };

  const backfill = async () => {
    if (paused) return;
    const b = body();
    if (!b || !document.body.contains(b)) { cleanup(); return; }
    try {
      const d = await http.get(`/api/logs?since=${since}&limit=200`);
      if (paused || el.dataset.view !== myView) return; // navigated/paused mid-await
      appendLogs(d.logs || []);
      if (d.next !== undefined) since = d.next;
    } catch (e) { if (el.dataset.view === myView) showError(e.message); }
  };

  // Primary path: SSE. On error, close it (stop the browser's native
  // auto-reconnect), fall back to polling and retry re-opening every 10s.
  const openSSE = () => {
    if (paused) return;
    closeSSE();
    if (typeof EventSource === 'undefined') { startPolling(); return; }
    let ev;
    try { ev = new EventSource('/api/logs/events'); } catch (_) { startPolling(); return; }
    es = ev;
    ev.onopen = () => { stopPolling(); stopRetry(); setStatus('logs.live'); };
    ev.onmessage = (e) => {
      const b = body();
      if (!b || !document.body.contains(b)) { cleanup(); return; }
      try {
        const d = JSON.parse(e.data);
        if (d && Array.isArray(d.logs)) appendLogs(d.logs);
        else if (d && d.level) appendLogs([d]);
      } catch (_) { /* non-JSON frame — ignore */ }
    };
    ev.onerror = () => {
      closeSSE();
      startPolling();
      if (!retry) retry = setTimeout(() => { retry = null; openSSE(); }, 10000);
    };
  };

  const pause = () => {
    paused = true;
    cleanup();
    setStatus('logs.paused');
    pauseBtn.textContent = t('logs.resume');
  };
  const resume = () => {
    paused = false;
    pauseBtn.textContent = t('logs.pause');
    backfill();   // catch up on lines emitted while paused
    openSSE();
  };

  const clearLogs = () => {
    lines.length = 0;
    seen.clear();
    const b = body();
    if (b) b.innerHTML = '';   // ring has no clear API — display-only clear
  };

  pauseBtn.addEventListener('click', () => (paused ? resume() : pause()));
  clearBtn.addEventListener('click', clearLogs);
  levelSel.addEventListener('change', () => { minLv = levelSel.value; renderAll(); });
  window.addEventListener('hashchange', cleanup, { once: true });

  backfill();   // history on entry (also first poll in fallback mode)
  openSSE();    // live tail
}
