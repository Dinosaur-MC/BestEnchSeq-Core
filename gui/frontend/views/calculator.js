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

// 图标 URL：DOM <img> 与 Canvas createImageBitmap 统一同一编码
// （encodeURIComponent；vanilla 短 id 为恒等变换）。
function iconUrl(id) { return `/public/vendor/icons/${encodeURIComponent(id)}.png`; }

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

// 最终物品 PPN：web 流程 build_target 不设 prior_penalty → target_item.ppn 恒
// 0，回退用末步 result 的 prior_penalty（末步 result 空时保持 0）。非零
// target_item.ppn（CLI/真实后端路径）照用。视觉卡、复制文本与截图共用此口径。
function finalPpn(sol) {
  const t = sol.target_item;
  if (t && (t.prior_penalty ?? 0) !== 0) return t.prior_penalty;
  const steps = sol.steps || [];
  const last = steps.length ? steps[steps.length - 1] : null;
  if (last && last.result && !itemEmpty(last.result))
    return last.result.prior_penalty ?? 0;
  return 0;
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
  const icon = `<img src="${iconUrl(itemIconId(item))}" alt="" ` +
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
// item card (reuses itemCardHtml — no duplicated icon/badge markup). PPN 经
// finalPpn 回退（web 流程 target_item.ppn 恒 0 → 末步 result 的 prior_penalty）。
function finalItemHtml(sol) {
  if (!sol.target_item || itemEmpty(sol.target_item)) return '';
  return `<div class="res-finalwrap">` +
    `<span class="res-stepno-hollow" aria-hidden="true">✓</span>` +
    `<div class="res-final">` +
    `<div class="flabel">${esc(t('res.forge_result'))}</div>` +
    itemCardHtml({ ...sol.target_item, prior_penalty: finalPpn(sol) }, 'final') +
    `</div></div>`;
}

// Algorithm info + action buttons (copy text / save image). The buttons are
// bound to onCopy/onSave in renderSolution after insert (events-on-insert).
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

// ── T4: 复制文本（ItemParser 语法）+ Canvas 截图导出 ────────────────────────
// buildCopyText 产出可回贴 CLI 的纯数据文本（不本地化——与 CLI 机器格式一致，
// 物品段完全符合 ItemParser.h 语法，可被 --target 直接解析）。renderCanvas 按
// v12 布局 2D 自绘 PNG：汇总条 + 步骤（序号圆/物品卡/成本列）+ 最终物品 +
// 算法行；图标经 createImageBitmap 加载同源 /public/vendor/icons/{id}.png，
// 加载失败跳过（名称徽章仍传达信息）。

// 物品 ItemParser 规格：`{shortId}[{ench}={level},...]{prior_penalty:N}`
// （PPN 非 0 才带后缀；书 → enchanted_book；魔咒按后端顺序，与 DOM 徽章一致；
// id 用短 id——ItemParser 的 minecraft: 前缀可选，与 CLI 示例一致）。
function itemSpec(item) {
  if (!item) return '';
  const id = (item.equipment && item.equipment.id)
    ? normalizeId(item.equipment.id) : 'enchanted_book';
  const enchs = (item.enchantments || [])
    .map((e) => `${normalizeId(e.id)}=${e.level}`).join(',');
  const ppn = (item.prior_penalty ?? 0) > 0
    ? `{prior_penalty:${item.prior_penalty}}` : '';
  return enchs ? `${id}[${enchs}]${ppn}` : `${id}${ppn}`;
}

// 完整复制文本：头部汇总 + 每步 A+B=C + 最终行（含算法元数据）。头部/尾部
// 模板固定（不随 UI 语言变化，机器可回贴）；result 空（A+B= 无 C）省略 " = C"。
export function buildCopyText(sol) {
  const steps = sol.steps || [];
  const m = sol.metadata || {};
  const lines = [];
  lines.push(`MC ${sol.platform || '—'} · ${state.profileVersion || '—'} · ` +
    `${steps.length} 步 · 等级 ${sol.total_exp_level_cost ?? '?'} · ` +
    `EXP ${sol.total_exp_cost ?? '?'}`);
  for (let i = 0; i < steps.length; i++) {
    const s = steps[i];
    const c = !itemEmpty(s.result) ? ` = ${itemSpec(s.result)}` : '';
    lines.push(`${i + 1}: ${itemSpec(s.item_a)} + ${itemSpec(s.item_b)}${c}` +
      `  (等级 ${s.exp_level_cost ?? '?'}, EXP ${s.exp_cost ?? '?'})`);
  }
  const tail = [];
  // PPN 与视觉卡同口径：target_item.ppn 恒 0 时回退末步 result 的 prior_penalty。
  if (sol.target_item && !itemEmpty(sol.target_item))
    tail.push(`最终: ${itemSpec({ ...sol.target_item, prior_penalty: finalPpn(sol) })}`);
  const algo = [];
  if (m.algorithm_name) algo.push(m.algorithm_name);
  if (m.algorithm_version) algo.push(m.algorithm_version);
  if (algo.length) tail.push(`算法 ${algo.join(' ')}`);
  if (m.computation_time != null) tail.push(`耗时 ${m.computation_time}ms`);
  if (tail.length) lines.push(tail.join(' · '));
  return lines.join('\n');
}

// 剪贴板：navigator.clipboard 优先，失败回退 execCommand('copy')（临时
// textarea + select；非安全上下文/权限拒绝时仍可用）。
function legacyCopy(text) {
  return new Promise((resolve, reject) => {
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.setAttribute('readonly', '');
    ta.style.position = 'fixed';
    ta.style.top = '-1000px';
    ta.style.left = '-1000px';
    document.body.appendChild(ta);
    ta.select();
    ta.setSelectionRange(0, ta.value.length);
    let ok = false;
    try { ok = document.execCommand('copy'); } catch (_) { /* 某些引擎抛错 */ }
    document.body.removeChild(ta);
    if (ok) resolve(); else reject(new Error(t('res.copy_failed')));
  });
}

async function copyToClipboard(text) {
  if (navigator.clipboard && window.isSecureContext) {
    try { await navigator.clipboard.writeText(text); return; }
    catch (_) { /* 权限拒绝等 → 走回退 */ }
  }
  await legacyCopy(text);
}

// 成功反馈：按钮短暂显示 ✓ + 成功文案（复制 res.copied / 保存 res.saved），
// 1.5s 后还原文案/可点状态。
function flashBtn(btn, msgKey = 'res.copied') {
  const orig = btn.textContent;
  btn.textContent = '✓ ' + t(msgKey);
  btn.disabled = true;
  setTimeout(() => {
    btn.textContent = orig;
    btn.disabled = false;
  }, 1500);
}

async function onCopy(sol, btn) {
  if (btn.disabled) return;
  btn.disabled = true;
  try {
    await copyToClipboard(buildCopyText(sol));
    flashBtn(btn);
  } catch (e) {
    btn.disabled = false;
    showError(e.message || t('res.copy_failed'));
  }
}

async function onSave(sol, btn) {
  if (btn.disabled) return;
  btn.disabled = true;
  try {
    const canvas = await renderCanvas(sol);
    await saveCanvas(canvas);
    flashBtn(btn, 'res.saved');
  } catch (e) {
    btn.disabled = false;
    showError(e.message || t('res.save_failed'));
  }
}

// 截图保存：PNG blob → <a download> 点击下载；同一 blob 尽力复制图片到剪贴板
//（ClipboardItem 不支持时仅下载——下载已成功，复制失败不报错）。
async function saveCanvas(canvas) {
  const blob = await new Promise((resolve, reject) =>
    canvas.toBlob((b) => (b ? resolve(b) : reject(new Error(t('res.save_failed')))), 'image/png'));
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'besq-solution.png';
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
  try {
    if (navigator.clipboard && navigator.clipboard.write && window.ClipboardItem && window.isSecureContext)
      await navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]);
  } catch (_) { /* 不支持 → 仅下载 */ }
}

// ── Canvas 自绘（暗色主题，色值与 styles.css :root 变量一致）────────────────
const CV_W = 720;            // 固定宽；高按内容
const CV_PAD = 16;
const CV_GAP = 6;
const CV_ROW_GAP = 10;
const CV_CARD_MIN = 160;     // 与 DOM .res-item min-width 10rem 一致
const CV_CARD_MAX = 288;     // 与 DOM .res-item max-width 18rem 一致
const CV_CARD_PAD_X = 8;
const CV_CARD_PAD_Y = 7;
const CV_ICON = 24;
const CV_NO_D = 28;          // 序号圆直径（1.75rem）
const CV_OP_W = 22;          // "+"/"=" 列
const CV_COST_W = 74;        // 右对齐成本列
const CV_STRIP_H = 38;       // 汇总条高度（drawSummary 与 renderCanvas 布局同步）
const CV_TAIL_LH = 19;       // 尾部算法行行距（布局与绘制同步）
const CV_COST_2LINE = 42;    // 两行成本块预留：EXP 行底缘实测 ~40px（含 descent），
                             // 旧 26px 会被下一行卡片顶缘裁切
const CV_FINAL_LABEL_H = 22; // 最终物品标签行高
const CV_FINAL_WRAP_MAX = 384; // 最终物品包装卡宽上限
const CV_R = 6;              // 卡片圆角（--radius）
const CV_BADGE_H = 20;
const CV_BADGE_GAP = 4;
const CV_COLORS = {
  bg: '#1d1b18',             // --bg
  panel: '#2b2823',          // --panel
  panel2: '#3a362f',         // --panel-2
  border: '#524c41',         // --border
  text: '#e8e2d5',           // --text
  muted: '#9b937f',          // --muted
  accent: '#5cb85c',         // --accent
  accent2: '#2e9ad0',        // --accent-2
  danger: '#d9534f',         // --danger
};
const CV_FONT = '"Segoe UI", system-ui, sans-serif';
const cvScratch = document.createElement('canvas').getContext('2d');

function cvFont(px, weight) { return `${weight || ''} ${px}px ${CV_FONT}`.trim(); }
function cvM(text, font) { cvScratch.font = font; return cvScratch.measureText(text).width; }
function cvRR(ctx, x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}
function cvText(ctx, text, x, y, color, font) {
  ctx.font = font;
  ctx.fillStyle = color;
  ctx.textBaseline = 'top';
  ctx.textAlign = 'left';
  ctx.fillText(text, x, y);
}

// 物品卡测量：头部（icon+名称+PPN 徽章）与附魔徽章网格（每行最多 3 个、超出
// 卡片上限的徽章省略号截断，与 DOM 3 列网格同语义）。返回 {w,h,name,ppn,ppnW,rows}。
// 导出供 .temp/verify_t4.py 复算 canvas 布局（纯函数，无副作用）。
export function measureItemCard(item) {
  const font15 = cvFont(15, '600');
  const font11 = cvFont(11, '400');
  const font13 = cvFont(13, '400');
  const maxInner = CV_CARD_MAX - 2 * CV_CARD_PAD_X;
  const ppn = tf('res.ppn', String(item.prior_penalty ?? 0));
  const ppnW = cvM(ppn, font11) + 10;
  // 长名截断上限并入 PPN 徽章位（审查 I2：漏减 ppnW 时长名钻到徽章下方）。
  const nameLimit = maxInner - CV_ICON - 2 * CV_GAP - ppnW;
  let name = itemName(item);
  if (cvM(name, font15) > nameLimit) {
    while (name.length > 1 && cvM(name + '…', font15) > nameLimit) name = name.slice(0, -1);
    name += '…';
  }
  const headW = CV_ICON + CV_GAP + cvM(name, font15) + CV_GAP + ppnW;
  const rows = [];
  let cur = [], curW = 0;
  for (const e of item.enchantments || []) {
    let text = `${displayName(e.id, shortId(e.id))} ${toRoman(e.level)}`;
    let w = cvM(text, font13) + 16;
    if (w > maxInner) {          // 单个超长附魔名 → 省略号截断
      while (text.length > 1 && cvM(text + '…', font13) + 16 > maxInner) text = text.slice(0, -1);
      text += '…';
      w = cvM(text, font13) + 16;
    }
    if (cur.length === 3 || (cur.length && curW + CV_GAP + w > maxInner)) {
      rows.push(cur); cur = []; curW = 0;
    }
    cur.push({ text, w });
    curW += cur.length > 1 ? CV_GAP + w : w;
  }
  if (cur.length) rows.push(cur);
  const rowsW = rows.reduce((m, r) =>
    Math.max(m, r.reduce((s, b) => s + b.w + CV_BADGE_GAP, -CV_BADGE_GAP)), 0);
  const w = Math.min(CV_CARD_MAX, Math.max(CV_CARD_MIN, headW, rowsW) + 2 * CV_CARD_PAD_X);
  const h = 2 * CV_CARD_PAD_Y + CV_ICON +
    (rows.length ? CV_GAP + rows.length * CV_BADGE_H + (rows.length - 1) * CV_BADGE_GAP : 0);
  return { w, h, name, ppn, ppnW, rows };
}

// 画一张物品卡（x,y 左上角；w 画布宽度；over → 红描边，isC → 绿描边+淡绿底）。
// 图标只记录位置（jobs），异步加载完成后统一叠画。
function drawItemCard(ctx, item, mc, x, y, w, over, isC, jobs) {
  const h = mc.h;
  cvRR(ctx, x, y, w, h, CV_R);
  ctx.fillStyle = isC && !over ? 'rgba(92,184,92,0.08)' : CV_COLORS.panel2;
  ctx.fill();
  ctx.strokeStyle = over ? CV_COLORS.danger : (isC ? CV_COLORS.accent : CV_COLORS.border);
  ctx.lineWidth = 1;
  ctx.stroke();
  // 收缩适配只缩放卡片宽、不重排徽章行——内容统一裁剪在卡片边界内（审查 I2：
  // 与 DOM 溢出裁切同语义；图标由 jobs 异步叠画、位置恒在卡内，不受影响）。
  ctx.save();
  ctx.beginPath();
  ctx.rect(x, y, w, h);
  ctx.clip();
  const iy = y + CV_CARD_PAD_Y;
  jobs.push({
    x: x + CV_CARD_PAD_X, y: iy,
    id: (item.equipment && item.equipment.id) ? normalizeId(item.equipment.id) : 'enchanted_book',
  });
  cvText(ctx, mc.name, x + CV_CARD_PAD_X + CV_ICON + CV_GAP, iy + 3, CV_COLORS.text, cvFont(15, '600'));
  const py = iy + (CV_ICON - 16) / 2;
  const px = x + w - CV_CARD_PAD_X - mc.ppnW;
  cvRR(ctx, px, py, mc.ppnW, 16, 4);
  ctx.strokeStyle = CV_COLORS.border;
  ctx.lineWidth = 1;
  ctx.stroke();
  cvText(ctx, mc.ppn, px + 5, py + 2, CV_COLORS.muted, cvFont(11, '400'));
  let by = iy + CV_ICON + CV_GAP;
  for (const row of mc.rows) {
    let bx = x + CV_CARD_PAD_X;
    for (const b of row) {
      cvRR(ctx, bx, by, b.w, CV_BADGE_H, 4);
      ctx.fillStyle = CV_COLORS.panel2;
      ctx.fill();
      ctx.strokeStyle = CV_COLORS.border;
      ctx.lineWidth = 1;
      ctx.stroke();
      cvText(ctx, b.text, bx + 8, by + 3, CV_COLORS.text, cvFont(13, '400'));
      bx += b.w + CV_BADGE_GAP;
    }
    by += CV_BADGE_H + CV_BADGE_GAP;
  }
  ctx.restore();
}

function drawOp(ctx, ch, x, cy) {
  ctx.font = cvFont(20, '700');
  ctx.fillStyle = CV_COLORS.muted;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(ch, x + CV_OP_W / 2, cy + 1);
  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
}

function drawStepNo(ctx, n, x, y, over) {
  const r = CV_NO_D / 2;
  ctx.beginPath();
  ctx.arc(x + r, y + r, r, 0, Math.PI * 2);
  ctx.fillStyle = over ? CV_COLORS.danger : CV_COLORS.accent;
  ctx.fill();
  ctx.fillStyle = '#fff';
  ctx.font = cvFont(15, '700');
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(String(n), x + r, y + r + 1);
  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
}

function drawHollowCheck(ctx, x, y) {
  const r = CV_NO_D / 2;
  ctx.beginPath();
  ctx.arc(x + r, y + r, r, 0, Math.PI * 2);
  ctx.strokeStyle = CV_COLORS.border;
  ctx.lineWidth = 1;
  ctx.stroke();
  ctx.fillStyle = CV_COLORS.muted;
  ctx.font = cvFont(15, '700');
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText('✓', x + r, y + r + 1);
  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
}

// 成本列（右对齐）：等级 N（超限红）+ EXP N。over 由调用方传入（measureRow
// 单一事实源，不再重复计算阈值）。
function drawCost(ctx, s, x2, y, over) {
  const label = t('res.level_label');
  const val = String(s.exp_level_cost ?? '?');
  const expLabel = t('res.exp_label').replace('{0}', '');
  const exp = String(s.exp_cost ?? '?');
  const vf = cvFont(14, '700');
  cvText(ctx, label, x2 - cvM(label, cvFont(12, '400')) - cvM(val, vf), y + 4, CV_COLORS.muted, cvFont(12, '400'));
  cvText(ctx, val, x2 - cvM(val, vf), y + 2, over ? CV_COLORS.danger : CV_COLORS.accent2, vf);
  const ef = cvFont(12, '400');
  cvText(ctx, expLabel + exp, x2 - cvM(expLabel + exp, ef), y + 24, CV_COLORS.muted, ef);
}

// 汇总条文本段（与 DOM summaryRowHtml 同源：t()/tf()，数值蓝色加粗）。
function summaryTokens(sol, steps) {
  const tok = (text, c, b) => ({ t: text, c, b });
  const out = [tok(tf('res.mc_platform', sol.platform || '—', state.profileVersion || '—'), CV_COLORS.text, false)];
  const addPair = (label, n) => {
    out.push(tok(' | ', CV_COLORS.border, false));
    out.push(tok(label, CV_COLORS.text, false));
    out.push(tok(String(n), CV_COLORS.accent2, true));
  };
  addPair(t('res.steps').replace('{0}', ''), steps.length);
  addPair(t('res.levels').replace('{0}', ''), sol.total_exp_level_cost ?? '?');
  addPair(t('res.exp_total').replace('{0}', ''), sol.total_exp_cost ?? '?');
  return out;
}

function drawSummary(ctx, tokens, anyOver, y) {
  const stripW = CV_W - 2 * CV_PAD;
  const stripH = 38;
  cvRR(ctx, CV_PAD, y, stripW, stripH, CV_R);
  ctx.fillStyle = CV_COLORS.panel;
  ctx.fill();
  ctx.strokeStyle = CV_COLORS.border;
  ctx.lineWidth = 1;
  ctx.stroke();
  let x = CV_PAD + 12;
  const ty = y + 10;
  for (const tok of tokens) {
    const font = cvFont(14, tok.b ? '700' : '400');
    ctx.font = font;
    ctx.fillStyle = tok.c;
    ctx.textBaseline = 'top';
    ctx.fillText(tok.t, x, ty);
    x += ctx.measureText(tok.t).width;
  }
  if (anyOver) {
    const txt = t('res.too_expensive');
    const bw = cvM(txt, cvFont(12, '600')) + 16;
    const bh = 20;
    const bx = x + 10;
    const by = ty + (18 - bh) / 2;
    cvRR(ctx, bx, by, bw, bh, 10);
    ctx.fillStyle = 'rgba(217,83,79,0.15)';
    ctx.fill();
    ctx.strokeStyle = CV_COLORS.danger;
    ctx.lineWidth = 1;
    ctx.stroke();
    ctx.font = cvFont(12, '600');
    ctx.fillStyle = CV_COLORS.danger;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    ctx.fillText(txt, bx + bw / 2, by + 4);
    ctx.textAlign = 'left';
  }
}

// 一行步骤的几何：A+B=C 三卡 + 运算符 + 成本列。卡片总宽超出可用宽度时按
// 比例收缩（下限 CV_CARD_MIN）；仍放不下一行则成本列换行到第二行右对齐。
// 导出供 .temp/verify_t4.py 复算布局（纯函数，无副作用）。
export function measureRow(step) {
  const a = measureItemCard(step.item_a);
  const b = measureItemCard(step.item_b);
  const hasC = !itemEmpty(step.result);
  const c = hasC ? measureItemCard(step.result) : null;
  const rowH = Math.max(CV_NO_D, a.h, b.h, c ? c.h : 0);
  const cardsW = a.w + b.w + (c ? c.w : 0);
  const avail = CV_W - 2 * CV_PAD - CV_NO_D - 3 * CV_GAP - 2 * CV_OP_W;
  const sw = cardsW > avail ? avail / cardsW : 1;
  const aw = Math.max(CV_CARD_MIN, a.w * sw);
  const bw = Math.max(CV_CARD_MIN, b.w * sw);
  const cw = c ? Math.max(CV_CARD_MIN, c.w * sw) : 0;
  const aX = CV_PAD + CV_NO_D + CV_GAP;
  const op1X = aX + aw + CV_GAP;
  const bX = op1X + CV_OP_W + CV_GAP;
  const op2X = bX + bw + CV_GAP;
  const cX = op2X + CV_OP_W + CV_GAP;
  const costX = CV_W - CV_PAD - CV_COST_W;
  const oneLine = cX + cw + CV_GAP + CV_COST_W <= CV_W - CV_PAD;
  return { over: (step.exp_level_cost ?? 0) >= TOO_EXPENSIVE_LEVEL, a, b, c, hasC, aw, bw, cw, aX, bX, cX, op1X, op2X, costX, rowH, oneLine };
}

// 图标位图缓存（同 id 只 fetch 一次）。
const iconCache = new Map();
async function iconBitmap(id) {
  if (!iconCache.has(id))
    iconCache.set(id, (async () => {
      try {
        const res = await fetch(iconUrl(id));
        if (!res.ok) return null;
        return await createImageBitmap(await res.blob());
      } catch (_) { return null; }
    })());
  return iconCache.get(id);
}

// 渲染一张截图画布：先纯测量布局（固定宽 720，高按内容），再绘制；图标
// 异步加载完成后叠画（加载失败/404 跳过）。返回 HTMLCanvasElement。
export async function renderCanvas(sol) {
  const steps = sol.steps || [];
  const m = sol.metadata || {};
  const rows = steps.map(measureRow);
  const ySummary = CV_PAD;
  let y = ySummary + CV_STRIP_H + CV_ROW_GAP;
  const yRows = y;
  let blockH = 0;
  // 两行成本块行高预留 CV_COST_2LINE（EXP 行底缘实测 ~40px；旧 26px 被下一行卡片顶缘裁切）。
  for (const r of rows) { r.rowY = yRows + blockH; blockH += r.rowH + (r.oneLine ? 0 : CV_COST_2LINE) + CV_ROW_GAP; }
  y = yRows + blockH;
  const yFinal = y;
  // PPN 与视觉卡同口径：target_item.ppn 恒 0 时回退末步 result 的 prior_penalty。
  const fItem = (sol.target_item && !itemEmpty(sol.target_item))
    ? { ...sol.target_item, prior_penalty: finalPpn(sol) } : null;
  let fH = 0;
  if (fItem && !itemEmpty(fItem)) {
    const fc = measureItemCard(fItem);
    fH = CV_FINAL_LABEL_H + fc.h + 12;   // 标签行 + 包装卡
    y += fH + CV_ROW_GAP;
  }
  const yTail = y;
  const tailLines = [];
  let line = '';
  if (m.algorithm_name) line += tf('res.algorithm', m.algorithm_name);
  if (m.algorithm_version) line += (line ? ' · ' : '') + tf('res.version', m.algorithm_version);
  if (line) tailLines.push(line);
  if (m.computation_time != null) tailLines.push(tf('res.wall_time', String(m.computation_time)));
  const nTail = tailLines.length + (steps.length === 0 && sol.is_success !== false ? 1 : 0);
  const H = yTail + nTail * CV_TAIL_LH + CV_PAD;

  const canvas = document.createElement('canvas');
  canvas.width = CV_W;
  canvas.height = H;
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = CV_COLORS.bg;
  ctx.fillRect(0, 0, CV_W, H);
  const jobs = [];

  if (sol.is_success === false) {
    cvText(ctx, t('calc.infeasible'), CV_PAD, ySummary + 10, CV_COLORS.danger, cvFont(14, '600'));
  } else {
    drawSummary(ctx, summaryTokens(sol, steps),
      steps.some((s) => (s.exp_level_cost ?? 0) >= TOO_EXPENSIVE_LEVEL), ySummary);
  }

  for (let i = 0; i < rows.length; i++) {
    const r = rows[i];
    const st = steps[i];
    const rowY = r.rowY;
    drawStepNo(ctx, i + 1, CV_PAD, rowY + (r.rowH - CV_NO_D) / 2, r.over);
    drawItemCard(ctx, st.item_a, r.a, r.aX, rowY, r.aw, r.over, false, jobs);
    drawOp(ctx, '+', r.op1X, rowY + r.rowH / 2);
    drawItemCard(ctx, st.item_b, r.b, r.bX, rowY, r.bw, r.over, false, jobs);
    drawOp(ctx, '=', r.op2X, rowY + r.rowH / 2);
    if (r.hasC) drawItemCard(ctx, st.result, r.c, r.cX, rowY, r.cw, r.over, true, jobs);
    drawCost(ctx, st, r.costX, r.oneLine ? rowY : rowY + r.rowH + 4, r.over);
  }

  if (fItem && !itemEmpty(fItem)) {
    const fc = measureItemCard(fItem);
    const wrapW = Math.min(CV_FINAL_WRAP_MAX, fc.w) + 16;
    const wrapH = fc.h + 12;
    cvText(ctx, t('res.forge_result'), CV_PAD, yFinal + 2, CV_COLORS.muted, cvFont(12, '400'));
    const cx0 = CV_PAD + CV_NO_D + CV_GAP;
    const cy0 = yFinal + CV_FINAL_LABEL_H;
    cvRR(ctx, cx0, cy0, wrapW, wrapH, CV_R);
    ctx.fillStyle = CV_COLORS.panel;
    ctx.fill();
    ctx.strokeStyle = CV_COLORS.border;
    ctx.lineWidth = 1;
    ctx.stroke();
    drawItemCard(ctx, fItem, fc, cx0 + 8, cy0 + 6, Math.min(CV_FINAL_WRAP_MAX, fc.w), false, false, jobs);
    drawHollowCheck(ctx, CV_PAD, cy0 + (wrapH - CV_NO_D) / 2);
  }

  let ty = yTail;
  if (steps.length === 0 && sol.is_success !== false) {
    cvText(ctx, t('calc.already_met'), CV_PAD, ty + 2, CV_COLORS.muted, cvFont(13, '400'));
    ty += CV_TAIL_LH;
  }
  for (const ln of tailLines) {
    cvText(ctx, ln, CV_PAD, ty + 2, CV_COLORS.muted, cvFont(13, '400'));
    ty += CV_TAIL_LH;
  }

  for (const j of jobs) {
    const bmp = await iconBitmap(j.id);
    if (!bmp) continue;
    ctx.imageSmoothingEnabled = false;   // 16px 原图放大保持像素风（同 DOM）
    ctx.drawImage(bmp, j.x, j.y, CV_ICON, CV_ICON);
    ctx.imageSmoothingEnabled = true;
  }
  return canvas;
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
  // T4: real copy/save actions; each button is disabled while its operation is
  // in flight so a double-click cannot fire two writes.
  const copyBtn = card.querySelector('.res-tail button.copy');
  const saveBtn = card.querySelector('.res-tail button.save');
  if (copyBtn) copyBtn.addEventListener('click', () => onCopy(sol, copyBtn));
  if (saveBtn) saveBtn.addEventListener('click', () => onSave(sol, saveBtn));
}

// Shared terminal-state rendering for the completed `result` payload (the
// OutputFormatter JSON), whether it arrives over SSE or via the poll fallback.
// Multiple solutions (result.solutions.length > 1) render inside an mdui-tabs
// control — one "方案 N" tab + panel per solution — while a single solution
// keeps the plain stacked card (existing v12 layout). Panels carry the
// `slot="panel"` required by the mdui-tabs contract (tabs in the default slot,
// panels in the "panel" slot; value-paired).
function renderResult(result) {
  const el = document.getElementById('calc-results');
  if (!el) return;
  el.innerHTML = '';
  const sols = result.solutions || [];
  if (sols.length > 1) {
    const tabs = document.createElement('mdui-tabs');
    tabs.setAttribute('variant', 'secondary');
    tabs.setAttribute('value', 'sol-1');
    sols.forEach((_, i) => {
      const tab = document.createElement('mdui-tab');
      tab.setAttribute('value', `sol-${i + 1}`);
      tab.textContent = tf('res.solution_n', i + 1);
      tabs.appendChild(tab);
    });
    sols.forEach((sol, i) => {
      const panel = document.createElement('mdui-tab-panel');
      panel.setAttribute('slot', 'panel');
      panel.setAttribute('value', `sol-${i + 1}`);
      renderSolution(panel, sol);
      tabs.appendChild(panel);
    });
    el.appendChild(tabs);
  } else {
    sols.forEach((sol) => renderSolution(el, sol));
  }
  if (result.success === false && sols.length === 0)
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
  setRunning(false);   // terminal state (completed/failed/cancelled/error) → restore run/clear
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
      // Late-frame guard: an in-flight snapshot dispatched before this task was
      // cancelled/replaced must not drive the view a newer run owns.
      if (id !== currentTask) return;
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
      // Late-error guard (mirrors the success path above): an in-flight GET
      // rejecting after this task was cancelled/replaced must not restore the
      // buttons, clear the newer run's pollTimer, or surface a stale error.
      if (id !== currentTask) return;
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
    es.close();              // own stream cleanup is always safe
    if (currentEs === es) currentEs = null;
    // Late-frame guard: a completed/failed frame from a superseded task (its
    // in-flight status snapshot / poll response resolving after cancel or a
    // newer run) must not overwrite the current result area. The own-stream
    // cleanup above still runs, but nothing renders and the newer run's
    // pollTimer is left untouched.
    if (id !== currentTask) return;
    clearInterval(pollTimer); // a terminal render supersedes any poll fallback
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
      // The same currentTask guard applies: a diag pull resolving after a
      // newer run started must not pollute its diagnostics card.
      http.get(`/api/tasks/${id}`)
        .then((st) => { if (id === currentTask) renderDiagExit(st.diag_exit); })
        .catch(() => {});
    });
  });
  es.addEventListener('failed', (ev) => {
    if (!document.body.contains(bar)) { es.close(); return; } // view torn down
    let data;
    try { data = JSON.parse(ev.data); } catch (_) { data = { error: String(ev.data) }; }
    settle(() => {
      finishProgress();
      showError(data.error || t('calc.no_result'));
      http.get(`/api/tasks/${id}`)
        .then((st) => { if (id === currentTask) renderDiagExit(st.diag_exit); })
        .catch(() => {});
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
    // on GET /api/tasks/{id} for exactly this reason). Only when this task is
    // still the current one — a stale stream error must not hijack the view.
    if (!settled && id === currentTask) startPoll(id);
  };

  // Safety net: an instant solve ("目标已达成" 0-step) may emit its terminal
  // frame before the EventSource connects — the completed/failed frames are
  // only delivered to subscribers present at publish time. One status snapshot
  // catches that case; `settled` prevents it from double-rendering with SSE.
  (async () => {
    try {
      const st = await http.get(`/api/tasks/${id}`);
      // Late-frame guard: the snapshot may resolve after this task was
      // cancelled/replaced — a completed status for a superseded task must
      // not overwrite the current run's result area.
      if (id !== currentTask) return;
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

// Running-state buttons: while a task is in flight #calc-run and #calc-clear
// are disabled and #calc-cancel is the only live action. Restoring re-runs
// updateSolveState so a conflict block on #calc-run (selections changed
// mid-solve) survives the restore.
function setRunning(on) {
  const clear = document.getElementById('calc-clear');
  const cancel = document.getElementById('calc-cancel');
  if (clear) clear.disabled = on;
  if (cancel) cancel.disabled = !on;
  if (on) {
    const run = document.getElementById('calc-run');
    if (run) run.disabled = true;
  } else {
    updateSolveState();
  }
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
  const icon = `<img src="${iconUrl(state.item)}" ` +
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
    const icon = `<img src="${iconUrl(short)}" ` +
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
  // Task lifecycle state never survives navigation: without this, an old
  // task's safety-net snapshot / poll response could render into the new
  // view, and a stale pollTimer could outlive the view it drives.
  currentTask = null;
  currentEs = null;
  clearInterval(pollTimer);

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
          <button id="calc-cancel" class="secondary" disabled>${t('calc.cancel')}</button>
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
    // A stale stream/poll must never outlive its run (cancel closes them, but
    // keep the ordering safe for any edge state) — then take the buttons to
    // the running state for the duration of the task.
    if (currentEs) { currentEs.close(); currentEs = null; }
    clearInterval(pollTimer);
    // POST in-flight has no task id yet: drop the superseded id so cancel is
    // a no-op (it would otherwise DEL the old task and restore the buttons
    // early), and stale late-frames hit the id guard. startSSE/startPoll set
    // currentTask to the new id once the POST resolves.
    currentTask = null;
    setRunning(true);
    try {
      // POST /api/tasks → 202 {task_id} + Location: /api/tasks/{id}
      const post = await http.post('/api/tasks', buildTask());
      startSSE(post.task_id || post.id);
    } catch (e) {
      setRunning(false);
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
