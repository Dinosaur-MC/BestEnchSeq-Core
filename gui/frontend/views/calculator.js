// Calculator view — zero-input enchantment calculator.
// Item picker (mdui-dropdown + icons from /public) → per-enchantment level
// button groups (target / current) → POST /api/tasks and subscribe to the
// task's SSE event stream (GET /api/tasks/{id}/events), falling back to
// polling GET /api/tasks/{id} whenever the stream fails. Renders the
// OutputFormatter JSON result (see OutputFormatter::format_json).
//
// Backend contract (T5):
//   GET /api/profiles/{key}/enchantables/{item} → bare array of
//   EnchInfo::to_json() entries: {id, name, max_level, multiplier,
//   is_treasure, exclusive_set, ...}; `enchanted_book` returns the full
//   registry; unknown profile/item → 404.
import { http, showError, clearError, esc } from '../api.js';
import { t, tf } from '../i18n.js';
import { displayName } from '../names_zh.js';

let pollTimer = null;
let currentTask = null;
let currentEs = null;   // live EventSource, so cancel() can close it

// ── Zero-input state (module level; render() resets) ────────────────
const state = {
  key: '',              // active profile key (from /api/status)
  itemId: '',           // selected item NSID ("minecraft:diamond_sword")
  item: '',             // selected item short id ("diamond_sword")
  algorithm: 'dp_merge',
  useSource: false,
  allowIncompat: false,
  items: [],            // picker entries: [{id, name}] (equipments + book)
  enchantables: [],     // raw /enchantables response for the current item
  sel: new Map(),       // shortId → {target, source, id}
  conflicts: new Map(), // shortId → [shortId of exclusive-set members, ...]
  profileVersion: '',   // active profile metadata version ("1.21.6" etc.; "" → "—")
};

// Backend path params are matched literally (no URL-decoding), and profile
// keys / NSIDs keep their ':'. Encode only characters unsafe in a URL path
// while preserving ':' and the RFC 3986 unreserved set (same helper as
// profiles.js).
const URL_SAFE_RE = /^[A-Za-z0-9\-._~:]+$/;
function encSeg(s) {
  let out = '';
  for (const ch of String(s)) out += URL_SAFE_RE.test(ch) ? ch : encodeURIComponent(ch);
  return out;
}

// Strip a leading '#' (legacy exclusive-set syntax) and the default
// "minecraft:" namespace → compact key used across DOM data attributes,
// the sel map and the conflicts map.
function normalizeId(id) {
  let s = String(id || '');
  if (s.startsWith('#')) s = s.slice(1);
  if (s.startsWith('minecraft:')) s = s.slice('minecraft:'.length);
  return s;
}

// Backend ids come fully-qualified ("minecraft:sharpness"); strip the default
// namespace for compact inline labels.
function shortId(id) {
  return id && id.startsWith('minecraft:') ? id.slice('minecraft:'.length) : id;
}

// I/II/III/IV/V/VI/VII/VIII/IX/X lookup, with a light extension past 10
// (modded profiles may exceed vanilla max_level); anything beyond 39 falls
// back to the raw number.
function toRoman(n) {
  const tab = ['', 'I', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'IX', 'X'];
  if (n >= 0 && n <= 10) return tab[n];
  if (n <= 39) return 'X'.repeat(Math.floor(n / 10)) + toRoman(n % 10);
  return String(n);
}

// ── Result area: A+B=C step cards (v12 layout) ───────────────────────────────
// Backend solution JSON (schema_version 1.1, OutputFormatter): each step
// carries item_a / item_b / result (the forged C) + exp_level_cost / exp_cost
// (exp_cost = cumulative XP to reach that anvil level, ExpCalculator);
// solutions carry platform / total_exp_level_cost / total_exp_cost /
// target_item / metadata{algorithm_name, algorithm_version, computation_time}.
// All backend strings pass through esc()/displayName() here; the innerHTML is
// built in one pass and buttons are bound right after (events-on-insert).

const TOO_EXPENSIVE_LEVEL = 39;  // anvil "Too Expensive" threshold: exp_level_cost >= 39 (same as CLI)

// Is an ItemView absent or empty (no equipment id AND no enchantments)? An
// unfilled result serializes as equipment:{id:""} with no enchantments — no C
// card is rendered for it, the step reads "A + B =" and ends there.
function itemEmpty(r) {
  if (!r) return true;
  const id = (r.equipment && r.equipment.id) || '';
  const en = r.enchantments || [];
  return !id && en.length === 0;
}

// Icon path short id: equipment id for items, enchanted_book for books (a
// book's equipment is null). Missing icons 404 and the onerror hides the img.
function itemIconId(item) {
  if (item && item.equipment && item.equipment.id) return normalizeId(item.equipment.id);
  return 'enchanted_book';
}

// Display name for an ItemView (equipment name or the book label).
function itemName(item) {
  if (!item) return '?';
  if (item.equipment && item.equipment.id)
    return displayName(item.equipment.id, item.equipment.name || shortId(item.equipment.id)) || '?';
  return displayName('minecraft:enchanted_book', t('calc.book'));
}

// One item card: icon (title=NSID, hidden on 404) + name + PPN badge +
// enchantment badge grid. `cls` adds card classes ("c" result, "over" red).
// Icon size is fixed by CSS (.res-itemhead img), no inline styles.
function itemCardHtml(item, cls) {
  if (!item) return '';
  const nsid = (item.equipment && item.equipment.id) || 'minecraft:enchanted_book';
  const icon = `<img src="/public/vendor/icons/${esc(itemIconId(item))}.png" alt="" ` +
    `title="${esc(nsid)}" onerror="this.style.display='none'">`;
  const ppn = `<span class="res-ppn">${esc(tf('res.ppn', esc(String(item.prior_penalty ?? 0))))}</span>`;
  const enchs = (item.enchantments || []).map((e) =>
    `<span class="res-badge" title="${esc(e.id)}">` +
    `${esc(displayName(e.id, shortId(e.id)))} ${esc(toRoman(e.level))}</span>`).join('');
  return `<div class="res-item${cls ? ' ' + cls : ''}">` +
    `<div class="res-itemhead">${icon}<span class="nm">${esc(itemName(item))}</span>${ppn}</div>` +
    (enchs ? `<div class="res-enchs">${enchs}</div>` : '') +
    `</div>`;
}

// One forge step row: 序号圆 + A + B + = + C（result 可用时）+ 成本列。
function stepRowHtml(step, index) {
  const over = (step.exp_level_cost ?? 0) >= TOO_EXPENSIVE_LEVEL;
  const stepno = `<span class="res-stepno${over ? ' expensive' : ''}" ` +
    `title="${esc(tf('res.step_n', esc(String(index + 1))))}">${index + 1}</span>`;
  const a = itemCardHtml(step.item_a, over ? 'over' : '');
  const b = itemCardHtml(step.item_b, over ? 'over' : '');
  // Empty result → keep the "=" and leave the C column blank (a spacer cell
  // keeps the cost column in the 5rem track of the 7-column grid).
  const c = !itemEmpty(step.result)
    ? itemCardHtml(step.result, (over ? 'over ' : '') + 'c')
    : '<span class="res-item-gap" aria-hidden="true"></span>';
  const cost = `<div class="res-cost">` +
    `<div><span class="lbl">${esc(t('res.level_label'))}</span> ` +
    `<span class="val${over ? ' over' : ''}">${esc(String(step.exp_level_cost ?? '?'))}</span></div>` +
    `<div class="exp" title="${esc(tf('res.exp_hint', esc(String(step.exp_level_cost ?? 0)), esc(String(step.exp_cost ?? 0))))}">` +
    `${esc(tf('res.exp_label', esc(String(step.exp_cost ?? 0))))}</div></div>`;
  return `<div class="res-step">${stepno}${a}<span class="res-op">+</span>${b}` +
    `<span class="res-op">=</span>${c}${cost}</div>`;
}

// Summary strip: MC platform · profile version | 步骤 N | 等级 X | EXP Y |
// too-expensive badge (red when any step reaches level 39).
// Numeric placeholders are spliced in from the view (t() + template), never
// passed as HTML through tf().
function summaryRowHtml(sol, steps) {
  const anyOver = steps.some((s) => (s.exp_level_cost ?? 0) >= TOO_EXPENSIVE_LEVEL);
  const parts = [
    `<span>${esc(tf('res.mc_platform', sol.platform || '—', state.profileVersion || '—'))}</span>`,
    `<span class="sep">|</span>`,
    `<span>${t('res.steps').replace('{0}', `<b>${esc(String(steps.length))}</b>`)}</span>`,
    `<span>${t('res.levels').replace('{0}', `<b>${esc(String(sol.total_exp_level_cost ?? '?'))}</b>`)}</span>`,
    `<span>${t('res.exp_total').replace('{0}', `<b>${esc(String(sol.total_exp_cost ?? '?'))}</b>`)}</span>`,
  ];
  if (anyOver)
    parts.push(`<span class="res-too-expensive">${esc(t('res.too_expensive'))}</span>`);
  return `<div class="res-summary">${parts.join('')}</div>`;
}

// Final item card: hollow ✓ circle + "锻造结果" label wrapping the target
// item card (reuses itemCardHtml — no duplicated icon/badge markup).
function finalItemHtml(sol) {
  if (!sol.target_item || itemEmpty(sol.target_item)) return '';
  return `<div class="res-finalwrap">` +
    `<span class="res-stepno-hollow" aria-hidden="true">✓</span>` +
    `<div class="res-final">` +
    `<div class="flabel">${esc(t('res.forge_result'))}</div>` +
    itemCardHtml(sol.target_item, 'final') +
    `</div></div>`;
}

// Algorithm info + action buttons. Copy/save are wired in T4 — this phase
// renders the controls and binds no-op handlers (structure + styles only).
function tailHtml(sol) {
  const m = sol.metadata || {};
  const metaLines = [];
  if (m.algorithm_name || m.algorithm_version) {
    const name = m.algorithm_name ? esc(tf('res.algorithm', m.algorithm_name)) : '';
    const ver = m.algorithm_version ? ` · ${esc(tf('res.version', m.algorithm_version))}` : '';
    metaLines.push(`<div>${name}${ver}</div>`);
  }
  if (m.computation_time != null)
    metaLines.push(`<div>${esc(tf('res.wall_time', esc(String(m.computation_time))))}</div>`);
  if (!metaLines.length) return '';
  return `<div class="res-tail">` +
    `<div class="res-meta">${metaLines.join('')}</div>` +
    `<div class="res-btns">` +
    `<button type="button" class="copy">${esc(t('res.copy'))}</button>` +
    `<button type="button" class="save">${esc(t('res.save_img'))}</button>` +
    `</div></div>`;
}

function renderSolution(el, sol) {
  const card = document.createElement('div');
  card.className = 'card res-solution';
  const steps = sol.steps || [];
  const infeasible = sol.is_success === false
    ? `<div class="diag-line diag-warn">${t('calc.infeasible')}</div>` : '';
  const zeroStep = sol.is_success !== false && !steps.length
    ? `<div class="res-already">${t('calc.already_met')}</div>` : '';
  // Infeasible runs carry no forge plan — skip the summary strip (empty counts
  // would be noise) and show only the 不可行 warning.
  const summary = infeasible ? '' : summaryRowHtml(sol, steps);
  card.innerHTML = `
    ${summary}
    ${infeasible}
    ${steps.length ? `<div class="res-steps">${steps.map(stepRowHtml).join('')}</div>` : zeroStep}
    ${finalItemHtml(sol)}
    ${tailHtml(sol)}`;
  el.appendChild(card);
  // T4 wires real actions; no-op placeholders keep the binding pattern in place.
  card.querySelectorAll('.res-tail button').forEach((b) => b.addEventListener('click', () => {}));
}

// Shared terminal-state rendering for the completed `result` payload (the
// OutputFormatter JSON), whether it arrives over SSE or via the poll fallback.
function renderResult(result) {
  const el = document.getElementById('calc-results');
  if (!el) return;
  el.innerHTML = '';
  (result.solutions || []).forEach((sol) => renderSolution(el, sol));
  if (result.success === false && (result.solutions || []).length === 0)
    showError(t('calc.unreachable'));
}

// ── Algorithm diagnostics (T2 backend fields) ─────────────────────────────
// GET /api/tasks/{id} carries `diagnostics` (event array) + terminal
// `diag_exit`; the SSE stream emits `event: diag` frames with the same event
// shapes ({kind:"progress"|"state"|"exit"}). There is no task-list endpoint,
// so the calculator result area is the display surface: live frames are
// appended as they arrive, and the exit summary (counters + KV) renders from
// whichever source lands first (SSE frame or status snapshot) — a per-run
// flag keeps the two from double-rendering.

let diagExitRendered = false;

function resetDiag() {
  diagExitRendered = false;
  const card = document.getElementById('calc-diag');
  const body = document.getElementById('calc-diag-body');
  if (!card || !body) return;
  body.innerHTML = '';
  card.style.display = 'none';
}

function showDiag() {
  const card = document.getElementById('calc-diag');
  if (card) card.style.display = '';
}

// Append one diag line, keeping the log bounded (drop oldest beyond 60).
function appendDiag(line) {
  const body = document.getElementById('calc-diag-body');
  if (!body) return;
  body.insertAdjacentHTML('beforeend', line);
  while (body.children.length > 60) body.removeChild(body.firstChild);
  showDiag();
}

// Escaped label for a backend diag value (int64 | string).
function diagVal(v) {
  return esc(v == null ? '' : String(v));
}

function renderDiagEvent(d) {
  if (!d || typeof d !== 'object') return;
  if (d.kind === 'exit') {
    if (diagExitRendered) return;
    diagExitRendered = true;
    const counters = d.counters || {};
    const kv = d.diag || {};
    const rows = Object.keys(kv).map((k) =>
      `<tr><td class="mono">${esc(k)}</td><td class="mono">${diagVal(kv[k])}</td></tr>`).join('');
    appendDiag(
      `<div class="diag-line">${t('diag.exit_status')}: <b>${esc(d.status ?? '')}</b>` +
      ` · ${t('calc.algorithm')}: ${esc(d.algorithm ?? '')}` +
      ` · ${t('diag.wall_ms')}: ${diagVal(d.wall_ms)}</div>` +
      `<div class="diag-line muted-line">${t('diag.nodes_visited')}: ${diagVal(counters.nodes_visited)}` +
      ` · ${t('diag.nodes_pruned')}: ${diagVal(counters.nodes_pruned)}` +
      ` · ${t('diag.steps_forged')}: ${diagVal(counters.steps_forged)}</div>` +
      (rows ? `<table class="diag-table"><tbody>${rows}</tbody></table>` : ''));
    return;
  }
  if (d.kind === 'state') {
    appendDiag(`<div class="diag-line">${t('diag.state')}: ${esc(d.from ?? '')} → ${esc(d.to ?? '')}</div>`);
    return;
  }
  if (d.kind === 'progress') {
    appendDiag(`<div class="diag-line">${t('diag.progress')}: ${esc(d.status ?? '')} ${d.pct ?? 0}%</div>`);
  }
}

// Render the terminal diag_exit object from a GET /api/tasks/{id} snapshot
// (idempotent via diagExitRendered, so an SSE frame that already rendered it
// is not duplicated).
function renderDiagExit(exit) {
  if (exit && !diagExitRendered) renderDiagEvent(exit);
}

// Parse + render one `event: diag` SSE frame.
function onDiagFrame(ev) {
  let d;
  try { d = JSON.parse(ev.data); } catch (_) { return; }
  renderDiagEvent(d);
}

function setProgress(frac) {
  const bar = document.getElementById('calc-progress');
  if (bar) bar.querySelector('div').style.width = `${Math.round((frac || 0) * 100)}%`;
}

function finishProgress() {
  const bar = document.getElementById('calc-progress');
  if (bar) bar.style.display = 'none';
  const label = document.getElementById('calc-status');
  if (label) label.textContent = '';
}

// Polling fallback: drives the same progress bar + terminal rendering as SSE,
// used when the event stream fails to connect or drops mid-task.
function startPoll(id) {
  clearInterval(pollTimer);
  currentTask = id;
  const bar = document.getElementById('calc-progress');
  if (!bar || !document.body.contains(bar)) return;
  bar.style.display = 'block';
  const label = document.getElementById('calc-status');
  label.textContent = t('calc.progress');

  pollTimer = setInterval(async () => {
    // The view may have been torn down by route() while we were waiting; stop
    // polling detached nodes instead of writing errors onto the new view.
    if (!document.body.contains(bar)) { clearInterval(pollTimer); return; }
    try {
      const st = await http.get(`/api/tasks/${id}`);
      setProgress(st.progress);
      if (st.state === 'completed') {
        clearInterval(pollTimer);
        finishProgress();
        renderResult(st.result);
        renderDiagExit(st.diag_exit);
      } else if (st.state === 'failed') {
        clearInterval(pollTimer);
        finishProgress();
        showError(st.error || t('calc.no_result'));
        renderDiagExit(st.diag_exit);
      } else if (st.state === 'cancelled') {
        clearInterval(pollTimer);
        finishProgress();
      }
    } catch (e) {
      clearInterval(pollTimer);
      finishProgress();
      showError(e.message);
    }
  }, 500);
}

// Subscribe to a task's SSE stream. `settled` guards against double terminal
// rendering when a poll safety-net and a delivered frame race.
function startSSE(id) {
  clearInterval(pollTimer);
  currentTask = id;
  const bar = document.getElementById('calc-progress');
  if (!bar) return;
  const label = document.getElementById('calc-status');
  label.textContent = t('calc.progress');
  bar.style.display = 'block';
  setProgress(0);

  let settled = false;
  const settle = (fn) => {
    if (settled) return;
    settled = true;
    clearInterval(pollTimer); // a terminal render supersedes any poll fallback
    es.close();
    if (currentEs === es) currentEs = null;
    fn();
  };
  const es = new EventSource(`/api/tasks/${id}/events`);
  currentEs = es;

  es.addEventListener('progress', (ev) => {
    if (!document.body.contains(bar)) { es.close(); return; }
    let data;
    try { data = JSON.parse(ev.data); } catch (_) { return; }
    setProgress(data.progress);
  });
  es.addEventListener('completed', (ev) => {
    if (!document.body.contains(bar)) { es.close(); return; } // view torn down
    let data;
    try { data = JSON.parse(ev.data); } catch (_) { return; }
    settle(() => {
      finishProgress();
      renderResult(data.result);
      // The completed frame carries no diagnostics — pull diag_exit from the
      // status snapshot (idempotent; a diag exit frame may have landed first).
      http.get(`/api/tasks/${id}`).then((st) => renderDiagExit(st.diag_exit)).catch(() => {});
    });
  });
  es.addEventListener('failed', (ev) => {
    if (!document.body.contains(bar)) { es.close(); return; } // view torn down
    let data;
    try { data = JSON.parse(ev.data); } catch (_) { data = { error: String(ev.data) }; }
    settle(() => {
      finishProgress();
      showError(data.error || t('calc.no_result'));
      http.get(`/api/tasks/${id}`).then((st) => renderDiagExit(st.diag_exit)).catch(() => {});
    });
  });
  // Algorithm diagnostics stream (T2): progress/state/exit events appended to
  // the diagnostics card live; the exit frame is deduped against the status
  // snapshot by diagExitRendered.
  es.addEventListener('diag', onDiagFrame);
  es.onerror = () => {
    es.close();
    if (currentEs === es) currentEs = null;
    // SSE unavailable/dropped → fall back to polling (status snapshot retained
    // on GET /api/tasks/{id} for exactly this reason).
    if (!settled) startPoll(id);
  };

  // Safety net: an instant solve ("目标已达成" 0-step) may emit its terminal
  // frame before the EventSource connects — the completed/failed frames are
  // only delivered to subscribers present at publish time. One status snapshot
  // catches that case; `settled` prevents it from double-rendering with SSE.
  (async () => {
    try {
      const st = await http.get(`/api/tasks/${id}`);
      if (st.state === 'completed') settle(() => { finishProgress(); renderResult(st.result); renderDiagExit(st.diag_exit); });
      else if (st.state === 'failed') settle(() => { finishProgress(); showError(st.error || t('calc.no_result')); renderDiagExit(st.diag_exit); });
      else if (st.state === 'cancelled') settle(finishProgress);
    } catch (_) { /* leave the stream to deliver */ }
  })();
}

// ── Zero-input helpers ───────────────────────────────────────────────

// Rebuild the conflicts map from the current enchantables' exclusive_set
// (normalized to short ids; self-references dropped).
function buildConflicts() {
  state.conflicts.clear();
  for (const e of state.enchantables) {
    const short = normalizeId(e.id);
    const set = (e.exclusive_set || []).map(normalizeId).filter((c) => c !== short);
    if (set.length) state.conflicts.set(short, set);
  }
}

function selTarget(id) {
  const s = state.sel.get(id);
  return s ? s.target : 0;
}

// Selected target pairs that violate the exclusive sets: shortId → Set of
// conflicting shortIds (both members have a selected target level).
function selectedConflicts() {
  const pairs = new Map();
  for (const [id, s] of state.sel) {
    if (!s.target) continue;
    for (const c of state.conflicts.get(id) || []) {
      if (selTarget(c) > 0) {
        if (!pairs.has(id)) pairs.set(id, new Set());
        pairs.get(id).add(c);
      }
    }
  }
  return pairs;
}

// Display names for conflicting ids (from the current enchantables), escaped.
function conflictNames(ids) {
  return [...ids].map((c) => {
    const e = state.enchantables.find((x) => normalizeId(x.id) === c);
    return esc(e ? displayName(e.id, e.name) : c);
  }).join(', ');
}

// One level-button group (I..max_level) for `col` ∈ target|source.
function lvButtons(short, col, blocked) {
  const ench = state.enchantables.find((e) => normalizeId(e.id) === short);
  if (!ench) return '';
  const sel = state.sel.get(short) || {};
  let out = '';
  for (let lv = 1; lv <= ench.max_level; lv++) {
    const active = sel[col] === lv;
    const dis = blocked || (col === 'source' && !state.useSource);
    // data-dis distinguishes "blocked by an exclusive-set conflict" from the
    // whole column being inert (source column while "use current" is off) —
    // the conflict ones get the stronger disabled visual in styles.css.
    const reason = dis ? (blocked ? 'conflict' : 'column') : '';
    out += `<button type="button" class="lv${active ? ' active' : ''}" ` +
      `data-ench="${esc(short)}" data-col="${col}" data-lv="${lv}"` +
      `${reason ? ` data-dis="${reason}"` : ''}${dis ? ' disabled' : ''}>` +
      `${toRoman(lv)}</button>`;
  }
  return out;
}

function renderEnchTable() {
  const body = document.getElementById('calc-ench-body');
  if (!body) return;
  const pairs = selectedConflicts();
  const rows = state.enchantables.map((e) => {
    const short = normalizeId(e.id);
    const sel = state.sel.get(short) || {};
    const conf = pairs.get(short);
    const isSelected = sel.target > 0;
    // Without "allow incompatible", an unselected enchant whose exclusive
    // partner is already selected cannot be picked — its target buttons are
    // disabled until the conflict is resolved (the selected side stays
    // clickable so the user can deselect it).
    const targetBlocked = !state.allowIncompat && !isSelected &&
      (state.conflicts.get(short) || []).some((c) => selTarget(c) > 0);
    const hint = conf && conf.size
      ? `<div class="conflict-hint">${t('calc.exclusive')}: ${conflictNames(conf)}</div>` : '';
    return `<tr class="${conf ? 'conflict' : ''}">
      <td>${esc(displayName(e.id, e.name || short))}${e.is_treasure ? `<span class="treasure-badge">${t('calc.treasure')}</span>` : ''}${hint}</td>
      <td class="mono">${esc(e.multiplier ?? '')}</td>
      <td>${lvButtons(short, 'target', targetBlocked)}</td>
      <td>${lvButtons(short, 'source', !state.useSource)}</td>
    </tr>`;
  }).join('');
  body.innerHTML = rows ||
    `<tr><td colspan="4" class="empty">${t('calc.no_enchantments')}</td></tr>`;
  body.querySelectorAll('button.lv').forEach((b) => b.addEventListener('click', () => {
    toggleLevel(b.dataset.ench, b.dataset.col, Number(b.dataset.lv));
  }));
}

function toggleLevel(short, col, lv) {
  const s = state.sel.get(short) || { target: 0, source: 0, id: short };
  s[col] = s[col] === lv ? 0 : lv;
  state.sel.set(short, s);
  renderEnchTable();  // re-render reflects highlights / disabled / conflict rows
  updateStatusBar();
  updateSolveState();
}

// "Current (n) → target (m)" summary; 0 counts render as t('calc.none').
function updateStatusBar() {
  const bar = document.getElementById('calc-status-bar');
  if (!bar) return;
  const n = [...state.sel.values()].filter((s) => s.target > 0).length;
  const m = [...state.sel.values()].filter((s) => s.source > 0).length;
  bar.innerHTML =
    `<span class="pill current">${t('calc.current')} (${m || t('calc.none')})</span>` +
    `<span>→</span>` +
    `<span class="pill">${t('calc.target')} (${n || t('calc.none')})</span>`;
}

// Solve button + conflict hint: a selected exclusive pair blocks solving
// unless "allow incompatible" is on (the hint then stays as row-level text).
function updateSolveState() {
  const run = document.getElementById('calc-run');
  const hint = document.getElementById('calc-solve-hint');
  if (!run || !hint) return;
  const blocked = !state.allowIncompat && selectedConflicts().size > 0;
  run.disabled = blocked;
  hint.textContent = blocked ? t('calc.solve_conflict') : '';
  hint.style.display = blocked ? '' : 'none';
}

// Current item icon + name inside the dropdown trigger button. Icons come
// from the embedded 16x16 set (/public/vendor/icons); non-vanilla ids 404 and
// the onerror handler hides the img.
function updateTrigger() {
  const span = document.getElementById('calc-item-trigger-span');
  if (!span) return;
  const entry = state.items.find((it) => String(it.id) === state.itemId);
  const label = entry ? displayName(entry.id, entry.name) : state.item;
  const icon = `<img src="/public/vendor/icons/${esc(state.item)}.png" ` +
    `alt="" onerror="this.style.display='none'">`;
  span.innerHTML = `${icon}${esc(label)}`;
}

// Algorithm picker: dp_merge first, then the backend order.
function fillAlgorithms(list) {
  const sel = document.getElementById('calc-algorithm');
  if (!sel) return;
  const names = ['dp_merge', ...list.filter((n) => n !== 'dp_merge')];
  sel.innerHTML = names.map((n) => `<mdui-menu-item value="${esc(n)}">${esc(n)}</mdui-menu-item>`).join('');
  sel.setAttribute('value', 'dp_merge');
  state.algorithm = 'dp_merge';
}

// Item picker: equipments + enchanted_book, each with its icon from /public
// (hidden on 404/embedded so the name remains). Clicks re-load the table.
function fillItemMenu(el, myView, eqs) {
  const menu = document.getElementById('calc-item-menu');
  if (!menu) return;
  const raw = Array.isArray(eqs) ? eqs : (eqs && eqs.equipments) || [];
  state.items = raw
    .map((e) => ({ id: e.id, name: e.name }))
    .concat([{ id: 'minecraft:enchanted_book', name: t('calc.book') }]);
  menu.innerHTML = state.items.map((it) => {
    const short = normalizeId(it.id);
    const icon = `<img src="/public/vendor/icons/${esc(short)}.png" ` +
      `alt="" onerror="this.style.display='none'">`;
    return `<mdui-menu-item value="${esc(String(it.id))}">` +
      `<div slot="custom" class="calc-menu-item">${icon}<span>${esc(displayName(it.id, it.name || short))}</span></div>` +
      `</mdui-menu-item>`;
  }).join('');
  menu.querySelectorAll('mdui-menu-item').forEach((item) => {
    item.addEventListener('click', () => {
      const full = item.getAttribute('value');
      if (full) selectItem(el, myView, full);
    });
  });
}

// Select `fullId` (NSID): reset selections, refetch enchantables, re-render.
// Guards the view ownership on el.dataset.view across the await.
async function selectItem(el, myView, fullId) {
  state.itemId = fullId;
  state.item = normalizeId(fullId);
  state.sel.clear();
  updateTrigger();
  updateStatusBar();
  updateSolveState();
  const body = document.getElementById('calc-ench-body');
  if (body) body.innerHTML = '';
  try {
    const data = await http.get(`/api/profiles/${encSeg(state.key)}/enchantables/${encSeg(fullId)}`);
    if (el.dataset.view !== myView) return;
    state.enchantables = Array.isArray(data) ? data : [];
    buildConflicts();
    renderEnchTable();
    updateStatusBar();
    updateSolveState();
  } catch (e) {
    if (el.dataset.view !== myView) return;
    state.enchantables = [];
    renderEnchTable();
    showError(e.message);
  }
}

// Build the task body from the current selections. Full NSIDs are kept so
// modded namespaces survive; no profile field — the backend doesn't use it.
function buildTask() {
  const enchants = [];
  const source = [];
  for (const [short, s] of state.sel) {
    const id = s.id || short;
    if (s.target > 0) enchants.push({ id, level: s.target });
    if (state.useSource && s.source > 0) source.push({ id, level: s.source });
  }
  const task = {
    target: { item: state.itemId, enchants },
    algorithm: state.algorithm,
  };
  if (source.length) task.source = source;
  return task;
}

export async function render(el) {
  const myView = el.dataset.view;
  // Reset the zero-input state (selections never survive navigation).
  state.key = '';
  state.itemId = '';
  state.item = '';
  state.algorithm = 'dp_merge';
  state.useSource = false;
  state.allowIncompat = false;
  state.items = [];
  state.enchantables = [];
  state.sel.clear();
  state.conflicts.clear();
  state.profileVersion = '';

  el.innerHTML = `
    <h2>${t('calc.title')}</h2>
    <div class="card">
      <label>${t('calc.item')}</label>
      <mdui-dropdown id="calc-item-dd" placement="bottom-start">
        <mdui-button slot="trigger" id="calc-item-trigger">
          <span class="item-trigger" id="calc-item-trigger-span"></span>
        </mdui-button>
        <mdui-menu id="calc-item-menu"></mdui-menu>
      </mdui-dropdown>
      <div class="option-row">
        <label><mdui-switch id="calc-use-source"></mdui-switch><span>${t('calc.use_current')}</span></label>
        <label><mdui-switch id="calc-allow-incompat"></mdui-switch><span>${t('calc.allow_incompat')}</span></label>
        <label>${t('calc.algorithm')}<mdui-select id="calc-algorithm" class="algo-select" value="dp_merge"></mdui-select></label>
      </div>
    </div>
    <div class="calc-cols">
      <div class="calc-main card">
        <h3>${t('calc.enchantments')}</h3>
        <div class="table-scroll">
          <table>
            <thead><tr>
              <th></th>
              <th>${t('calc.weight')}</th>
              <th>${t('calc.target')}</th>
              <th>${t('calc.current')}</th>
            </tr></thead>
            <tbody id="calc-ench-body"></tbody>
          </table>
        </div>
      </div>
      <div class="calc-side card">
        <div class="status-bar" id="calc-status-bar"></div>
        <div class="btn-row">
          <button id="calc-run">${t('calc.run')}</button>
          <button id="calc-clear" class="secondary">${t('calc.clear')}</button>
          <button id="calc-cancel" class="secondary">${t('calc.cancel')}</button>
        </div>
        <div id="calc-solve-hint" class="conflict-hint" style="display:none"></div>
        <div id="calc-progress" class="progress" style="display:none"><div style="width:0%"></div></div>
        <div id="calc-status"></div>
      </div>
    </div>
    <div id="calc-results"></div>
    <div id="calc-diag" class="card" style="display:none">
      <h3>${t('diag.title')}</h3>
      <div id="calc-diag-body" class="mono"></div>
    </div>`;
  updateStatusBar();

  document.getElementById('calc-use-source').addEventListener('change', (ev) => {
    state.useSource = !!ev.target.checked;   // keeps already-picked source levels
    renderEnchTable();
  });
  document.getElementById('calc-allow-incompat').addEventListener('change', (ev) => {
    state.allowIncompat = !!ev.target.checked;
    renderEnchTable();
    updateSolveState();
  });
  document.getElementById('calc-algorithm').addEventListener('change', () => {
    state.algorithm = document.getElementById('calc-algorithm').value || 'dp_merge';
  });
  document.getElementById('calc-run').addEventListener('click', async () => {
    clearError();
    resetDiag();
    try {
      // POST /api/tasks → 202 {task_id} + Location: /api/tasks/{id}
      const post = await http.post('/api/tasks', buildTask());
      startSSE(post.task_id || post.id);
    } catch (e) {
      showError(e.message);
    }
  });
  document.getElementById('calc-clear').addEventListener('click', () => {
    state.sel.clear();
    renderEnchTable();
    updateStatusBar();
    updateSolveState();
  });
  document.getElementById('calc-cancel').addEventListener('click', async () => {
    if (currentTask) {
      if (currentEs) { currentEs.close(); currentEs = null; }
      try { await http.del(`/api/tasks/${currentTask}`); } catch (e) { /* ignore */ }
      clearInterval(pollTimer);
      finishProgress();
      currentTask = null;
    }
  });

  // Boot: active profile key → algorithms + equipments + profile metadata
  // (version for the summary strip "MC Java · <版本>") in parallel.
  try {
    const status = await http.get('/api/status');
    if (el.dataset.view !== myView) return;
    state.key = status.active_profile;
    const [algos, eqs, prof] = await Promise.all([
      http.get('/api/algorithms'),
      http.get(`/api/profiles/${encSeg(state.key)}/equipments`),
      // Version failure is non-fatal: stays "" → renders "—".
      http.get(`/api/profiles/${encSeg(state.key)}`).catch(() => null),
    ]);
    if (el.dataset.view !== myView) return;
    state.profileVersion = (prof && prof.version) || '';
    fillAlgorithms(Array.isArray(algos) ? algos : []);
    fillItemMenu(el, myView, eqs);
    const raw = Array.isArray(eqs) ? eqs : (eqs && eqs.equipments) || [];
    // Default: diamond_sword when present, else the first entry, else the book.
    const def = raw.find((e) => normalizeId(e.id) === 'diamond_sword') || raw[0] || null;
    if (def) selectItem(el, myView, def.id);
    else selectItem(el, myView, 'minecraft:enchanted_book');
  } catch (e) {
    showError(e.message);
  }
}
