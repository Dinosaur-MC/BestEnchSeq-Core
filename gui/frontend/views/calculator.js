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
import { t } from '../i18n.js';

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

// One backend item object → short inline label: "diamond_sword[sharpness 5]",
// or "[sharpness 3]" for a book. All id/name/enchant pieces are escaped here
// (the single sink for backend strings reaching the card HTML).
function itemLabel(item) {
  if (!item) return '?';
  const ench = (item.enchantments || []).map((e) => `${esc(shortId(e.id))} ${e.level}`).join(', ');
  if (item.is_book) return ench ? `[${ench}]` : t('calc.book');
  const base = esc(shortId((item.equipment && (item.equipment.id || item.equipment.name)))) || '?';
  return ench ? `${base}[${ench}]` : base;
}

function renderSolution(el, sol, index) {
  const card = document.createElement('div');
  card.className = 'card';
  // Steps carry item_a/item_b (both item objects) + exp_level_cost; there is
  // no per-step result field in the JSON, so no "→ result" arrow. item_a is
  // the target being upgraded; item_b is the book — rendered as just its label
  // (brackets distinguish a book, avoiding a bare "book=book" operand).
  const steps = (sol.steps || []).map((s, i) =>
    `<div class="step"><b>${t('calc.step')} ${i + 1}:</b> ` +
    `${t('calc.target')}=${itemLabel(s.item_a)} + ${itemLabel(s.item_b)} ` +
    `(${t('calc.cost')}: ${s.exp_level_cost ?? '?'})</div>`).join('');
  // final_item only exists on Solution::to_json(), not in the OutputFormatter
  // JSON this view consumes — render it only when the backend provides it.
  const finalItem = sol.final_item
    ? `<div class="mono">${t('calc.final_item')}: ${itemLabel(sol.final_item)}</div>` : '';
  card.innerHTML = `
    <h3>#${index + 1} — ${t('calc.total_cost')}: ${sol.total_exp_level_cost ?? '?'}</h3>
    ${steps || `<div>${t('calc.no_result')}</div>`}
    ${finalItem}`;
  el.appendChild(card);
}

// Shared terminal-state rendering for the completed `result` payload (the
// OutputFormatter JSON), whether it arrives over SSE or via the poll fallback.
function renderResult(result) {
  const el = document.getElementById('calc-results');
  if (!el) return;
  el.innerHTML = '';
  (result.solutions || []).forEach((sol, i) => renderSolution(el, sol, i));
  if (result.success === false && (result.solutions || []).length === 0)
    showError(t('calc.unreachable'));
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
      } else if (st.state === 'failed') {
        clearInterval(pollTimer);
        finishProgress();
        showError(st.error || t('calc.no_result'));
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
    settle(() => { finishProgress(); renderResult(data.result); });
  });
  es.addEventListener('failed', (ev) => {
    if (!document.body.contains(bar)) { es.close(); return; } // view torn down
    let data;
    try { data = JSON.parse(ev.data); } catch (_) { data = { error: String(ev.data) }; }
    settle(() => { finishProgress(); showError(data.error || t('calc.no_result')); });
  });
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
      if (st.state === 'completed') settle(() => { finishProgress(); renderResult(st.result); });
      else if (st.state === 'failed') settle(() => { finishProgress(); showError(st.error || t('calc.no_result')); });
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
    return esc(e && e.name ? e.name : c);
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
    out += `<button type="button" class="lv${active ? ' active' : ''}" ` +
      `data-ench="${esc(short)}" data-col="${col}" data-lv="${lv}"${dis ? ' disabled' : ''}>` +
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
      <td>${esc(e.name || short)}${e.is_treasure ? `<span class="treasure-badge">${t('calc.treasure')}</span>` : ''}${hint}</td>
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

// Current item icon + name inside the dropdown trigger button.
function updateTrigger() {
  const span = document.getElementById('calc-item-trigger-span');
  if (!span) return;
  const entry = state.items.find((it) => String(it.id) === state.itemId);
  const label = entry && entry.name ? entry.name : state.item;
  const icon = `<img src="/public/assets/minecraft/textures/item/${esc(state.item)}.png" ` +
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
    const icon = `<img src="/public/assets/minecraft/textures/item/${esc(short)}.png" ` +
      `alt="" onerror="this.style.display='none'">`;
    return `<mdui-menu-item value="${esc(String(it.id))}">` +
      `<div slot="custom" class="calc-menu-item">${icon}<span>${esc(it.name || short)}</span></div>` +
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
    <div id="calc-results"></div>`;
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

  // Boot: active profile key → algorithms + equipments → default item.
  try {
    const status = await http.get('/api/status');
    if (el.dataset.view !== myView) return;
    state.key = status.active_profile;
    const [algos, eqs] = await Promise.all([
      http.get('/api/algorithms'),
      http.get(`/api/profiles/${encSeg(state.key)}/equipments`),
    ]);
    if (el.dataset.view !== myView) return;
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
