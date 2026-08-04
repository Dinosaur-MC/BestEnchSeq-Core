// Calculator view — solve form, task submission, and solution timeline renderer.
// Submits POST /api/tasks and subscribes to the task's SSE event stream
// (GET /api/tasks/{id}/events), falling back to polling GET /api/tasks/{id}
// whenever the stream fails. Renders the OutputFormatter JSON result
// (see OutputFormatter::format_json).
import { http, showError, clearError, esc } from '../api.js';
import { t } from '../i18n.js';

let pollTimer = null;
let currentTask = null;
let currentEs = null;   // live EventSource, so cancel() can close it

// "sharpness=5,knockback=2" → [{id, level}, ...]
function enchantmentsToJSON(enchantments) {
  return enchantments
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean)
    .map((s) => {
      const [id, lvl] = s.split('=');
      const level = parseInt(lvl || '1', 10);
      return { id: id.trim(), level: Number.isFinite(level) ? level : 1 };
    });
}

function taskJSON() {
  const enchants = enchantmentsToJSON(document.getElementById('target-enchants').value);
  const source = enchantmentsToJSON(document.getElementById('source').value);
  const body = {
    target: {
      item: document.getElementById('target-item').value.trim() || 'diamond_sword',
      enchants,
    },
    algorithm: document.getElementById('algorithm').value || 'dp_merge',
  };
  if (source.length) body.source = source;
  return body;
}

// Backend ids come fully-qualified ("minecraft:sharpness"); strip the default
// namespace for compact inline labels.
function shortId(id) {
  return id && id.startsWith('minecraft:') ? id.slice('minecraft:'.length) : id;
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

export function render(el) {
  el.innerHTML = `
    <h2>${t('calc.title')}</h2>
    <div class="card">
      <label>${t('calc.target_item')}</label>
      <input id="target-item" value="diamond_sword">
      <label>${t('calc.target_enchants')}</label>
      <input id="target-enchants" placeholder="sharpness=5,knockback=2" value="sharpness=5">
      <label>${t('calc.source')}</label>
      <input id="source" placeholder="sharpness=2">
      <label>${t('calc.algorithm')}</label>
      <input id="algorithm" value="dp_merge">
      <div style="margin-top:12px">
        <button id="calc-run">${t('calc.run')}</button>
        <button id="calc-cancel" class="secondary">${t('calc.cancel')}</button>
      </div>
      <div id="calc-progress" class="progress" style="display:none"><div style="width:0%"></div></div>
      <div id="calc-status"></div>
    </div>
    <div id="calc-results"></div>`;

  document.getElementById('calc-run').addEventListener('click', async () => {
    clearError();
    try {
      // POST /api/tasks → 202 {task_id} + Location: /api/tasks/{id}
      const post = await http.post('/api/tasks', taskJSON());
      startSSE(post.task_id || post.id);
    } catch (e) {
      showError(e.message);
    }
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
}
