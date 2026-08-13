// history.js — Solve history view: paged list over GET /api/history with a
// 3s polling refresh (the former live log feed is gone — polling only).
// Pagination is server-side (offset/limit): the response carries
// {events, total, next_offset} so the pager never guesses offsets.
// Event types arrive as lowercase strings ("submitted"/"completed"/…),
// mapped to readable labels through i18n (fallback = raw type).
import { http, showError, esc } from '../api.js';
import { t, tf } from '../i18n.js';
import { pagerHtml, bindPager } from './pager.js';

const PAGE_SIZE = 20;
const POLL_MS = 3000;

// Event type → i18n key; unknown types fall back to the raw string.
const TYPE_KEYS = {
  submitted: 'hist.type_submitted',
  completed: 'hist.type_completed',
  failed: 'hist.type_failed',
  cancelled: 'hist.type_cancelled',
};

const pad2 = (n) => String(n).padStart(2, '0');
const fmtTs = (ms) => {
  const d = new Date(ms);
  return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ` +
         `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
};
const dash = '—';

const typeLabel = (type) => t(TYPE_KEYS[type] || type);

// One table row; every backend string goes through esc().
const rowHtml = (e) => {
  const isCompleted = e.type === 'completed';
  const cost = isCompleted ? String(e.total_level_cost) : dash;
  const dur = e.computation_ms > 0 ? `${e.computation_ms} ms` : dash;
  const err = e.type === 'failed' && e.error_message ? esc(e.error_message) : dash;
  return `<tr>
    <td class="mono">${esc(fmtTs(e.timestamp_ms))}</td>
    <td><span class="hist-type ${esc(e.type)}">${esc(typeLabel(e.type))}</span></td>
    <td class="mono">${esc(e.target || dash)}</td>
    <td>${esc(e.algorithm || dash)}</td>
    <td>${esc(e.mode || dash)}</td>
    <td class="mono">${cost}</td>
    <td class="mono">${dur}</td>
    <td class="hist-err">${err}</td>
  </tr>`;
};

export async function render(el) {
  const myView = el.dataset.view; // bail if route() re-stamps ownership mid-await
  let offset = 0;       // current page's offset (server-side cursor)
  let nextOffset = 0;   // next_offset from the last response (next page start)
  let page = 1;         // current page number (1-based, for the pager)
  let total = 0;        // all events (for "共 N 条" + last-page disable)
  let timer = null;
  let loading = false;  // a fetch is in flight (poll ticks skip while true)
  let reqSeq = 0;       // request generation: stale responses are discarded
  let lastKey = '';     // JSON key of the last rendered page (flicker-free refresh)

  const stopPoll = () => { if (timer) { clearInterval(timer); timer = null; } };
  const cleanup = () => stopPoll();
  const alive = () => document.body.contains(el);

  el.innerHTML = `<h2>${t('hist.title')}</h2><div class="card">
    <div class="hist-bar">
      <span class="hist-total" id="hist-total"></span>
      <span class="hist-status">${esc(tf('hist.auto', POLL_MS / 1000))}</span>
    </div>
    <div class="table-scroll">
      <table id="hist-table">
        <thead><tr>
          <th>${t('hist.time')}</th><th>${t('hist.type')}</th>
          <th>${t('hist.target')}</th><th>${t('hist.algorithm')}</th>
          <th>${t('hist.mode')}</th><th>${t('hist.cost')}</th>
          <th>${t('hist.duration')}</th><th>${t('hist.error')}</th>
        </tr></thead>
        <tbody id="hist-body"></tbody>
      </table>
    </div>
    <div id="hist-pager" class="pager"></div>
  </div>`;
  const body = () => document.getElementById('hist-body');
  const pagerEl = () => document.getElementById('hist-pager');
  const totalEl = () => document.getElementById('hist-total');

  const renderPager = () => {
    const totalPages = total === 0 ? 0 : Math.ceil(total / PAGE_SIZE);
    const html = pagerHtml(page, totalPages);
    const p = pagerEl();
    if (!p || !alive()) return;
    p.innerHTML = html;
    if (html) bindPager(p, (pg) => {
      // Next uses the server-provided next_offset; prev walks back a page.
      const off = pg > page && nextOffset > offset ? nextOffset : (pg - 1) * PAGE_SIZE;
      fetchPage(off); // user intent wins — no loading guard
    });
  };

  // Render the list from a fetched page (JSON key guard skips no-op polls).
  const renderPage = (events, nTotal, nNext, appliedOff) => {
    total = nTotal;
    nextOffset = nNext;
    const key = JSON.stringify({ appliedOff, events, total });
    if (key === lastKey) return; // nothing changed since last poll — no flicker
    lastKey = key;
    const b = body();
    if (!b || !alive()) return;
    b.innerHTML = events.length === 0
      ? `<tr><td colspan="8" class="empty">${esc(t('hist.empty'))}</td></tr>`
      : events.map(rowHtml).join('');
    const tEl = totalEl();
    if (tEl) tEl.textContent = tf('hist.total', total);
    renderPager();
  };

  const fetchPage = async (off) => {
    const my = ++reqSeq;
    loading = true;
    try {
      const d = await http.get(`/api/history?offset=${off}&limit=${PAGE_SIZE}`);
      if (el.dataset.view !== myView || my !== reqSeq) return; // stale response
      offset = off;
      page = Math.floor(off / PAGE_SIZE) + 1;
      renderPage(d.events || [], d.total || 0, d.next_offset || 0, off);
    } catch (e) {
      if (el.dataset.view === myView && my === reqSeq) showError(e.code, e.message);
    } finally {
      loading = false;
    }
  };

  // 3s poll: keeps new solves appearing without SSE. Stops when the view
  // is torn down (hashchange) or detached.
  const startPoll = () => {
    if (timer) return;
    timer = setInterval(() => {
      if (!alive()) { stopPoll(); return; }
      if (loading) return; // a fetch is already in flight — skip this tick
      fetchPage(offset);
    }, POLL_MS);
  };

  window.addEventListener('hashchange', cleanup, { once: true });
  await fetchPage(0);
  startPoll();
}
