// history.js — Solve history view: paged list over GET /api/history with a
// 3s polling refresh (the former live log feed is gone — polling only).
// Pagination is server-side (offset/limit): the response carries
// {events, total, next_offset} so the pager never guesses offsets.
// Event types arrive as lowercase strings ("submitted"/"completed"/…),
// mapped to readable labels through i18n (fallback = raw type).
//
// C1: completed events carry a `result` object — the full OutputFormatter
// JSON (the same source as the calculator's result area, so 方案详情 is
// identical). A 查看方案 button on the row toggles an inline expanded row.
import { http, showError, esc } from '../api.js';
import { t, tf } from '../i18n.js';
import { displayName } from '../names_zh.js';
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

// ── C1: solution detail rendering ─────────────────────────────────────
// Simplified from calculator.js (no cross-view import): same res-* CSS
// classes and field conventions (solutions[].steps[].item_a/item_b/result/
// exp_level_cost/exp_cost + total_exp_level_cost/total_exp_cost/metadata),
// icons omitted — history stays dependency-light.
const TOO_EXPENSIVE_LEVEL = 39; // anvil "Too Expensive" threshold (same as calc)

function toRoman(n) {
  const tab = ['', 'I', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'IX', 'X'];
  if (n >= 0 && n <= 10) return tab[n];
  if (n <= 39) return 'X'.repeat(Math.floor(n / 10)) + toRoman(n % 10);
  return String(n);
}

const shortId = (id) => id && id.startsWith('minecraft:') ? id.slice('minecraft:'.length) : id;

// Is an ItemView absent or empty (no equipment id AND no enchantments)?
function itemEmpty(r) {
  if (!r) return true;
  const id = (r.equipment && r.equipment.id) || '';
  const en = r.enchantments || [];
  return !id && en.length === 0;
}

// Item card body: name + PPN badge + enchantment badge grid (no sprite icon).
function solItemHtml(item) {
  if (!item) return '';
  const eq = item.equipment;
  const name = eq && eq.id
    ? (displayName(eq.id, eq.name || shortId(eq.id)) || shortId(eq.id))
    : (displayName('minecraft:enchanted_book', t('calc.book')) || t('calc.book'));
  const ppn = `<span class="res-ppn">${esc(tf('res.ppn', esc(String(item.prior_penalty ?? 0))))}</span>`;
  const enchs = (item.enchantments || []).map((e) =>
    `<span class="res-badge" title="${esc(e.id)}">` +
    `${esc(displayName(e.id, shortId(e.id)))} ${esc(toRoman(e.level))}</span>`).join('');
  return `<div class="res-itemhead">${esc(name)}${ppn}</div>` +
    (enchs ? `<div class="res-enchs">${enchs}</div>` : '');
}

function solCardHtml(item, cls) {
  if (!item) return '';
  return `<div class="res-item${cls ? ' ' + cls : ''}">${solItemHtml(item)}</div>`;
}

// 最终物品 PPN 口径与计算器页一致：target_item.ppn 恒 0（web 流程）时回退末步
// result 的 prior_penalty。
function solFinalPpn(sol) {
  const tgt = sol.target_item;
  if (tgt && (tgt.prior_penalty ?? 0) !== 0) return tgt.prior_penalty;
  const steps = sol.steps || [];
  const last = steps.length ? steps[steps.length - 1] : null;
  if (last && last.result && !itemEmpty(last.result)) return last.result.prior_penalty ?? 0;
  return 0;
}

// One forge step row: 序号圆 + A + B + = + C + 成本列（res-steps grid 直嵌）。
function solStepHtml(step, index) {
  const over = (step.exp_level_cost ?? 0) >= TOO_EXPENSIVE_LEVEL;
  const stepno = `<span class="res-stepno${over ? ' expensive' : ''}" ` +
    `title="${esc(tf('res.step_n', esc(String(index + 1))))}">${index + 1}</span>`;
  const a = solCardHtml(step.item_a, over ? 'over' : '');
  const b = solCardHtml(step.item_b, over ? 'over' : '');
  const c = !itemEmpty(step.result)
    ? solCardHtml(step.result, (over ? 'over ' : '') + 'c')
    : '<span class="res-item-gap" aria-hidden="true"></span>';
  const cost = `<div class="res-cost">` +
    `<div><span class="lbl">${esc(t('res.level_label'))}</span> ` +
    `<span class="val${over ? ' over' : ''}">${esc(String(step.exp_level_cost ?? '?'))}</span></div>` +
    `<div class="exp">${esc(tf('res.exp_label', esc(String(step.exp_cost ?? 0))))}</div></div>`;
  return `<div class="res-step">${stepno}${a}<span class="res-op">+</span>${b}` +
    `<span class="res-op">=</span>${c}${cost}</div>`;
}

// One solution block: 方案 N 头（多方案时）+ 汇总条 + 步骤/已达/不可行 +
// 最终物品 + 算法元数据。`count` = solutions 总数（>1 才显示方案头）。
function solBlockHtml(sol, index, count) {
  const steps = sol.steps || [];
  const m = sol.metadata || {};
  const anyOver = steps.some((s) => (s.exp_level_cost ?? 0) >= TOO_EXPENSIVE_LEVEL);
  const head = count > 1 ? `<div class="res-solhead">${esc(tf('res.solution_n', index + 1))}</div>` : '';
  const parts = [
    `<span>${t('res.steps').replace('{0}', `<b>${esc(String(steps.length))}</b>`)}</span>`,
    `<span>${t('res.levels').replace('{0}', `<b>${esc(String(sol.total_exp_level_cost ?? '?'))}</b>`)}</span>`,
    `<span>${t('res.exp_total').replace('{0}', `<b>${esc(String(sol.total_exp_cost ?? '?'))}</b>`)}</span>`,
  ];
  if (anyOver) parts.push(`<span class="res-too-expensive">${esc(t('res.too_expensive'))}</span>`);
  const summary = `<div class="res-summary">${parts.join('<span class="sep">|</span>')}</div>`;
  const infeasible = sol.is_success === false
    ? `<div class="diag-line diag-warn">${esc(t('calc.infeasible'))}</div>` : '';
  const zeroStep = sol.is_success !== false && !steps.length
    ? `<div class="res-already">${esc(t('calc.already_met'))}</div>` : '';
  const finalItem = (sol.target_item && !itemEmpty(sol.target_item))
    ? `<div class="res-finalwrap"><span class="res-stepno-hollow" aria-hidden="true">✓</span>` +
      `<div class="res-final"><div class="flabel">${esc(t('res.forge_result'))}</div>` +
      `${solItemHtml({ ...sol.target_item, prior_penalty: solFinalPpn(sol) })}</div></div>` : '';
  const meta = (m.algorithm_name || m.algorithm_version)
    ? `<div class="res-tail"><div class="res-meta">` +
      `${m.algorithm_name ? esc(tf('res.algorithm', m.algorithm_name)) : ''}` +
      `${m.algorithm_version ? ` · ${esc(tf('res.version', m.algorithm_version))}` : ''}` +
      `</div></div>` : '';
  return `${head}${summary}${infeasible}` +
    `${steps.length ? `<div class="res-steps">${steps.map(solStepHtml).join('')}</div>` : zeroStep}` +
    `${finalItem}${meta}`;
}

// 展开区内容：result 根（OutputFormatter JSON）的 solutions 数组，逐方案卡片。
function detailHtml(result) {
  const sols = result.solutions || [];
  return sols.map((sol, i) =>
    `<div class="card res-solution">${solBlockHtml(sol, i, sols.length)}</div>`).join('');
}

// One table row + its (hidden) detail row; every backend string goes through
// esc(). Completed events with a result get the 查看方案 toggle.
const rowHtml = (e) => {
  const isCompleted = e.type === 'completed';
  const cost = isCompleted ? String(e.total_level_cost) : dash;
  // 耗时 <1ms（目标已达成 0 步/快速求解）被毫秒截断为 0——Completed 显示
  // "<1 ms"（诚实反映截断语义），Failed/Cancelled 无耗时概念保持 '—'。
  const dur = isCompleted ? (e.computation_ms > 0 ? `${e.computation_ms} ms` : '<1 ms') : dash;
  const err = e.type === 'failed' && e.error_message ? esc(e.error_message) : dash;
  const hasResult = isCompleted && e.result && typeof e.result === 'object' &&
    Array.isArray(e.result.solutions) && e.result.solutions.length > 0;
  const action = hasResult
    ? `<button type="button" class="hist-view" data-seq="${e.seq}">${esc(t('hist.view_solution'))}</button>`
    : dash;
  return `<tr data-seq="${e.seq}">
    <td class="mono">${esc(fmtTs(e.timestamp_ms))}</td>
    <td><span class="hist-type ${esc(e.type)}">${esc(typeLabel(e.type))}</span></td>
    <td class="mono">${esc(e.target || dash)}</td>
    <td>${esc(e.algorithm || dash)}</td>
    <td>${esc(e.mode || dash)}</td>
    <td class="mono">${cost}</td>
    <td class="mono">${dur}</td>
    <td class="hist-err">${err}</td>
    <td class="hist-action">${action}</td>
  </tr>` + (hasResult
    ? `<tr class="hist-detail" data-seq="${e.seq}" hidden><td colspan="9">${detailHtml(e.result)}</td></tr>`
    : '');
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
          <th>${t('hist.action')}</th>
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
      ? `<tr><td colspan="9" class="empty">${esc(t('hist.empty'))}</td></tr>`
      : events.map(rowHtml).join('');
    // C1: inline expand/collapse — each button toggles its paired detail row.
    b.querySelectorAll('button.hist-view').forEach((btn) => {
      btn.addEventListener('click', () => {
        const detailRow = b.querySelector(`tr.hist-detail[data-seq="${btn.dataset.seq}"]`);
        if (!detailRow) return;
        const open = detailRow.hidden;
        detailRow.hidden = !open;
        btn.textContent = open ? t('hist.collapse') : t('hist.view_solution');
      });
    });
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
