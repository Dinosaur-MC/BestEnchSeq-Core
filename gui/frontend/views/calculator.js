// Calculator view — solve form, task poll, and solution timeline renderer.
// Talks to /api/calculator (WebSolveService + ApiCalculator) and renders the
// OutputFormatter JSON result (see OutputFormatter::format_json).
import { http, showError, clearError } from '../api.js';
import { t } from '../i18n.js';

let pollTimer = null;
let currentTask = null;

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

// Escape a string for safe interpolation into innerHTML (single escape; the
// output only ever lands in innerHTML, never textContent).
function esc(s) {
  return String(s).replace(/[&<>"']/g,
    (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
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

function startPoll(id) {
  clearInterval(pollTimer);
  currentTask = id;
  const bar = document.getElementById('calc-progress');
  const label = document.getElementById('calc-status');
  label.textContent = t('calc.progress');
  bar.style.display = 'block';

  pollTimer = setInterval(async () => {
    // The view may have been torn down by route() while we were waiting; stop
    // polling detached nodes instead of writing errors onto the new view.
    if (!document.body.contains(bar)) { clearInterval(pollTimer); return; }
    try {
      const st = await http.get(`/api/calculator/${id}`);
      bar.querySelector('div').style.width = `${Math.round(st.progress * 100)}%`;
      if (st.state === 'completed') {
        clearInterval(pollTimer);
        bar.style.display = 'none';
        label.textContent = '';
        document.getElementById('calc-results').innerHTML = '';
        (st.result.solutions || []).forEach((sol, i) => renderSolution(document.getElementById('calc-results'), sol, i));
        if (st.result.success === false && (st.result.solutions || []).length === 0)
          showError(t('calc.unreachable'));
      } else if (st.state === 'failed') {
        clearInterval(pollTimer);
        bar.style.display = 'none';
        label.textContent = '';
        showError(st.error || t('calc.no_result'));
      } else if (st.state === 'cancelled') {
        clearInterval(pollTimer);
        bar.style.display = 'none';
        label.textContent = '';
      }
    } catch (e) {
      clearInterval(pollTimer);
      bar.style.display = 'none';
      showError(e.message);
    }
  }, 200);
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
      const post = await http.post('/api/calculator', taskJSON());
      startPoll(post.task_id);
    } catch (e) {
      showError(e.message);
    }
  });
  document.getElementById('calc-cancel').addEventListener('click', async () => {
    if (currentTask) {
      try { await http.del(`/api/calculator/${currentTask}`); } catch (e) { /* ignore */ }
      clearInterval(pollTimer);
      document.getElementById('calc-progress').style.display = 'none';
      const st = document.getElementById('calc-status');
      if (st) st.textContent = '';
      currentTask = null;
    }
  });
}
