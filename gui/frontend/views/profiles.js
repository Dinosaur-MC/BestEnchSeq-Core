// profiles.js — Profiles view against the resource-ized contract:
//   /api/profiles, /api/profiles/{key}[+ /activate /rename], and the per-key
//   sub-resources /enchantments|equipments|tags (+/{name}).
// Renders list/activate/fork-create/remove, a metadata panel (GET /api/profiles/
// {key}), and registry tables with add / remove / PATCH-edit. Equipment rows
// carry an item icon from /public (hidden on 404).
import { http, showError, clearError, esc } from '../api.js';
import { t, tf } from '../i18n.js';
import { displayName } from '../names_zh.js';
import { iconSpanHtml } from '../sprite.js';
import { paginate, pagerHtml, bindPager } from './pager.js';

let renderSeq = 0; // guards registry renders against overlapping async re-renders

// Backend path params are matched literally (no URL-decoding), and entry ids /
// profile names keep their ':' (NSIDs, arbitrary-string keys). So encode only
// the characters unsafe in a URL path — '#', '?', '%', space, quotes, '/' —
// while preserving ':' and the RFC 3986 unreserved set. A name the server can't
// address then fails loudly (404) instead of silently hitting another route
// (e.g. `foo#bar` stripping the fragment → empty list view).
const URL_SAFE_RE = /^[A-Za-z0-9\-._~:]+$/;
function encSeg(s) {
  let out = '';
  for (const ch of String(s)) out += URL_SAFE_RE.test(ch) ? ch : encodeURIComponent(ch);
  return out;
}

// Per-kind → plural wire segment (the resource-ized route segments).
const KINDS = {
  ench:  { plural: 'enchantments', label: 'prof.ench' },
  equip: { plural: 'equipments',   label: 'prof.equip' },
  tag:   { plural: 'tags',         label: 'prof.tag' },
};

// Registry icons come from the sprite sheet (sprite.js helpers); ids without
// a tile (modded) render '' — same hidden semantics as the old onerror-hide
// <img>, minus the per-id request.

// A minimal entry the backend's validation accepts. The en/equip/tag fields the
// registry requires differ — this seeds the add-form with just enough to
// create + list an entry. Vanilla-free: `category` is omitted (an empty string
// would fail NSID validation; the field is optional) and `supported_items` is
// an empty set.
function minimalEntry(kind, id) {
  if (kind === 'equip') return { id, name: id, max_durability: 100 };
  if (kind === 'tag') return { id, name: id };
  return { id, name: id, max_level: 1, multiplier: 1, supported_items: [] };
}

// Per-kind editable fields → the form's input set. `join: true` renders an
// array field as a comma-separated list; saving rebuilds the array. The
// PATCH/POST body must pass the backend schema (EnchInfo/Equipment/EquipmentTag)
// — missing fields default, wrong types reject (400).
const EDIT_FIELDS = {
  ench: [
    { key: 'id', label: 'prof.id', type: 'text' },
    { key: 'name', label: 'prof.name', type: 'text' },
    { key: 'max_level', label: 'prof.max_level', type: 'number' },
    { key: 'multiplier', label: 'prof.multiplier', type: 'number' },
    { key: 'supported_items', label: 'prof.supported_items', type: 'text', join: true },
    { key: 'is_treasure', label: 'prof.treasure', type: 'bool' },
  ],
  equip: [
    { key: 'id', label: 'prof.id', type: 'text' },
    { key: 'name', label: 'prof.name', type: 'text' },
    { key: 'max_durability', label: 'prof.max_durability', type: 'number' },
    { key: 'category', label: 'prof.category', type: 'text' },
  ],
  tag: [
    { key: 'id', label: 'prof.id', type: 'text' },
    { key: 'name', label: 'prof.name', type: 'text' },
  ],
};

// Inline field form for a single registry entry — replaces the v1 JSON
// textarea editor. `entry` is the full entry object (from the list response);
// saving PATCHes it back to /api/profiles/{key}/{plural}/{id} and re-renders.
// With `isNew` the form POSTs to the collection instead (add-via-form).
function openEditor(el, key, kind, entry, isNew) {
  const card = document.createElement('div');
  card.className = 'card';
  card.style.margin = '8px 0';
  const fields = EDIT_FIELDS[kind];
  let html = '';
  for (const f of fields) {
    const val = entry[f.key];
    let input;
    if (f.type === 'bool') {
      input = `<label class="check-label"><input type="checkbox" data-f="${f.key}"${val ? ' checked' : ''}>${t(f.label)}</label>`;
    } else if (f.type === 'number') {
      input = `<input type="number" data-f="${f.key}" value="${esc(val ?? '')}">`;
    } else if (f.join) {
      input = `<input data-f="${f.key}" value="${esc((val || []).join(', '))}" placeholder="minecraft:foo, #minecraft:bar">`;
    } else {
      input = `<input data-f="${f.key}" value="${esc(val ?? '')}"${!isNew && f.key === 'id' ? ' readonly' : ''}>`;
    }
    html += `<label>${t(f.label)}</label>${input}`;
  }
  card.innerHTML = `<div class="form-grid">${html}</div>
    <div class="btn-row">
      <button data-act="save">${t('prof.save')}</button>
      <button data-act="cancel" class="secondary">${t('prof.cancel')}</button>
    </div>`;
  card.querySelector('[data-act="cancel"]').addEventListener('click', () => card.remove());
  card.querySelector('[data-act="save"]').addEventListener('click', async () => {
    clearError();
    const patch = {};
    for (const f of fields) {
      const inputEl = card.querySelector(`[data-f="${f.key}"]`);
      if (f.type === 'bool') patch[f.key] = inputEl.checked;
      else if (f.type === 'number') patch[f.key] = inputEl.value === '' ? 0 : Number(inputEl.value);
      else if (f.join) patch[f.key] = inputEl.value.split(',').map((s) => s.trim()).filter(Boolean);
      else patch[f.key] = inputEl.value.trim();
    }
    // Empty optional text fields (e.g. `category`) must be omitted — an empty
    // string fails NSID validation; a missing field takes the schema default.
    for (const f of fields) {
      if (f.type === 'text' && f.key !== 'id' && f.key !== 'name' && patch[f.key] === '')
        delete patch[f.key];
    }
    if (!patch.id) { showError(t('prof.id_required')); return; }
    try {
      if (isNew) {
        await http.post(`/api/profiles/${encSeg(key)}/${KINDS[kind].plural}`, patch);
      } else {
        await http.patch(`/api/profiles/${encSeg(key)}/${KINDS[kind].plural}/${encSeg(entry.id)}`, patch);
      }
      card.remove();
      await renderRegistry(el, key, kind);
    } catch (e) { showError(e.message); }
  });
  el.appendChild(card);
  card.querySelector('input:not([readonly]), input[type="text"]')?.focus();
}

// Local pagination: one registry page renders PAGE_SIZE rows with a
// prev/next pager. Page state survives mutation re-renders within the same
// (profile, kind) and resets when the tab or profile changes.
const PAGE_SIZE = 50;
const regPage = { profile: '', kind: '', page: 1 };

// Render one registry (kind ∈ ench|equip|tag) for `profile` into `el`, which
// must be a dedicated container: the card replaces whatever was there so the
// add/remove/edit re-render never duplicates it.
async function renderRegistry(el, profile, kind) {
  const seq = ++renderSeq;
  el.replaceChildren();
  const wrap = document.createElement('div');
  wrap.className = 'card';
  const plural = KINDS[kind].plural;
  let data;
  try {
    data = await http.get(`/api/profiles/${encSeg(profile)}/${plural}`);
  } catch (e) {
    if (seq === renderSeq) { showError(e.message); el.appendChild(wrap); }
    return;
  }
  if (seq !== renderSeq) return; // a newer render superseded this one — don't append stale DOM
  // The new controller returns a bare array; older handlers wrapped it in an
  // object under the plural key — accept both so the view is resilient.
  const entries = Array.isArray(data) ? data : (data[plural] || []);
  if (regPage.profile !== profile || regPage.kind !== kind) {
    regPage.profile = profile; regPage.kind = kind; regPage.page = 1;
  }
  const pg = paginate(entries, regPage.page, PAGE_SIZE);
  const icon = kind === 'equip'
    ? (id) => iconSpanHtml(id, 20, '', 'margin-right:4px')
    : () => '';
  const rows = pg.items
    .map((e) => `<tr>
        <td>${icon(e.id)}${esc(e.id)}</td>
        <td>${esc(kind === 'tag' ? (e.name || '') : displayName(e.id, e.name || ''))}</td>
        <td>${esc(e.max_level ?? e.max_durability ?? '')}</td>
        <td>
          <button data-edit="${esc(e.id)}">${t('prof.edit')}</button>
          <button data-rm="${esc(e.id)}">${t('prof.remove')}</button>
        </td></tr>`)
    .join('');
  wrap.innerHTML = `
    <h3>${t('prof.' + kind)}</h3>
    <table><thead><tr><th>${t('prof.id')}</th><th>${t('prof.name')}</th><th>${t('prof.max_level')}</th><th></th></tr></thead>
    <tbody>${rows || '<tr><td colspan="4">—</td></tr>'}</tbody></table>
    ${pagerHtml(pg.page, pg.total)}
    <label>${t('prof.id')}</label><input class="add-id" placeholder="${t('prof.id_placeholder')}">
    <button class="add-row">${t('prof.add')}</button>`;
  bindPager(wrap, (p) => { regPage.page = p; renderRegistry(el, profile, kind); });
  wrap.querySelector('.add-row').addEventListener('click', () => {
    clearError();
    // Add-via-form: seed the form with the typed id (may be empty), the user
    // fills the rest, save POSTs to the collection.
    const id = wrap.querySelector('.add-id').value.trim();
    openEditor(el, profile, kind, minimalEntry(kind, id), true);
  });
  wrap.querySelectorAll('[data-rm]').forEach((b) => b.addEventListener('click', async () => {
    clearError();
    try {
      await http.del(`/api/profiles/${encSeg(profile)}/${plural}/${encSeg(b.dataset.rm)}`);
      await renderRegistry(el, profile, kind);
    } catch (e) { showError(e.message); }
  }));
  wrap.querySelectorAll('[data-edit]').forEach((b) => b.addEventListener('click', () => {
    clearError();
    const id = b.dataset.edit;
    const entry = entries.find((x) => x.id === id);
    if (entry) openEditor(el, profile, kind, entry, false);
  }));
  el.appendChild(wrap);
}

// Metadata panel — GET /api/profiles/{key} full metadata + editable deps.
function renderMeta(meta) {
  const panel = document.getElementById('prof-meta');
  if (!panel) return;
  const deps = (meta.dependencies || []).join(', ');
  panel.innerHTML = `
    <h3>${esc(meta.name || '')}</h3>
    <table class="meta-table">
      <tr><th>name</th><td>${esc(meta.name || '')}</td></tr>
      <tr><th>is_root</th><td>${meta.is_root ? 'yes' : 'no'}</td></tr>
      <tr><th>format</th><td>${esc(meta.format || '')}</td></tr>
      <tr><th>enchantments</th><td>${esc(meta.ench_count ?? '')}</td></tr>
      <tr><th>equipments</th><td>${esc(meta.eq_count ?? '')}</td></tr>
      <tr><th>tags</th><td>${esc(meta.tag_count ?? '')}</td></tr>
      <tr><th>dependencies</th><td><input class="deps-input" value="${esc(deps)}">
        <button class="deps-save">${t('prof.save')}</button></td></tr>
    </table>`;
  panel.querySelector('.deps-save').addEventListener('click', async () => {
    clearError();
    const key = panel.dataset.key;
    const raw = panel.querySelector('.deps-input').value;
    const deps = raw.split(',').map((s) => s.trim()).filter(Boolean);
    try {
      await http.patch(`/api/profiles/${encSeg(key)}`, { dependencies: deps });
      const meta2 = await http.get(`/api/profiles/${encSeg(key)}`);
      renderMeta(meta2);
    } catch (e) { showError(e.message); }
  });
}

export function render(el) {
  el.innerHTML = `<h2>${t('prof.title')}</h2>`;
  const list = document.createElement('div');
  list.className = 'card';
  const metaPanel = document.createElement('div');
  metaPanel.className = 'card';
  metaPanel.id = 'prof-meta';
  metaPanel.innerHTML = '<h3>…</h3>';
  const tabs = document.createElement('div');
  tabs.className = 'card';
  const regEl = document.createElement('div');

  // The selected profile key drives meta + registry rendering. Defaults to the
  // active profile; reselected after mutations.
  let current = null;

  const refreshRegistry = (key, kind) => renderRegistry(regEl, key, kind);

  const load = async () => {
    const data = await http.get('/api/profiles');
    // Per-profile metadata (small list) drives the delete guard: the root
    // profile's Remove button is disabled (its backend deletion is rejected
    // with 409 PROFILE_IS_ROOT anyway), every other profile confirms first.
    const metas = await Promise.all(data.profiles.map(async (p) => {
      try { return await http.get(`/api/profiles/${encSeg(p)}`); }
      catch (_) { return null; }
    }));
    const metaOf = (p) => metas[data.profiles.indexOf(p)];
    const isRoot = (p) => { const m = metaOf(p); return !!m && m.is_root === true; };
    list.innerHTML = `<table><thead><tr><th>${t('prof.name')}</th><th></th><th></th></tr></thead><tbody>` +
      data.profiles.map((p) => {
        const root = isRoot(p);
        return `<tr><td>${esc(p)}${p === data.active ? ` (${t('prof.active')})` : ''}</td>
          <td><button data-act="${esc(p)}">${t('prof.activate')}</button>
              <button data-view="${esc(p)}">${t('prof.view')}</button></td>
          <td><button data-ren="${esc(p)}">${t('prof.rename')}</button>
              <button data-rmp="${esc(p)}"${root ? ` disabled title="${esc(t('prof.root_locked'))}"` : ''}>${t('prof.remove')}</button></td></tr>`;
      }).join('') +
      `</tbody></table>
       <label>${t('prof.new_name')}</label><input class="fork-name">
       <button class="fork-btn">${t('prof.create')}</button>`;


    list.querySelectorAll('[data-view]').forEach((b) => b.addEventListener('click', async () => {    list.querySelectorAll('[data-act]').forEach((b) => b.addEventListener('click', async () => {
      clearError();
      const key = b.dataset.act;
      try {
        await http.post(`/api/profiles/${encSeg(key)}/activate`);
        current = key;
        await load();
        await selectProfile(key);
      } catch (e) { showError(e.message); }
    }));

      clearError();
      const key = b.dataset.view;
      current = key;
      await selectProfile(key);
    }));
    list.querySelectorAll('[data-rmp]').forEach((b) => b.addEventListener('click', async () => {
      clearError();
      const key = b.dataset.rmp;
      if (b.disabled) return;   // root: guarded on the backend, disabled here
      if (!window.confirm(tf('prof.confirm_remove', key))) return;
      try {
        await http.del(`/api/profiles/${encSeg(key)}`);
        const wasCurrent = current === key;
        if (wasCurrent) { current = null; metaPanel.innerHTML = '<h3>…</h3>'; regEl.replaceChildren(); }
        const active = await load();
        if (wasCurrent) { current = active; await selectProfile(active); }
      } catch (e) { showError(e.message); }
    }));
    list.querySelectorAll('[data-ren]').forEach((b) => b.addEventListener('click', () => {
      clearError();
      const oldKey = b.dataset.ren;
      const name = window.prompt('New profile name', oldKey);
      if (!name || name === oldKey) return;
      if (/[/#?%]/.test(name)) { showError(t('prof.invalid_name')); return; }
      (async () => {
        try {
          await http.post(`/api/profiles/${encSeg(oldKey)}/rename`, { name });
          if (current === oldKey) { current = name; await selectProfile(name); }
          await load();
        } catch (e) { showError(e.message); }
      })();
    }));
    list.querySelector('.fork-btn').addEventListener('click', async () => {
      clearError();
      const name = list.querySelector('.fork-name').value.trim();
      if (!name) return;
      // URL-hostile characters are rejected at the input so a created profile is
      // always addressable by the registry routes below.
      if (/[/#?%]/.test(name)) { showError(t('prof.invalid_name')); return; }
      try {
        // POST /api/profiles {source,dest} → 201 fork-create
        await http.post('/api/profiles', { source: data.active, dest: name });
        await load();
      } catch (e) { showError(e.message); }
    });
    return data.active;
  };

  // Select a profile: render its metadata panel + registry tabs (default ench).
  const selectProfile = async (key) => {
    metaPanel.dataset.key = key;
    tabs.innerHTML = Object.keys(KINDS)
      .map((k) => `<button class="tab-btn" data-tab="${k}">${t(KINDS[k].label)}</button>`).join('');
    tabs.querySelectorAll('.tab-btn').forEach((b) => b.addEventListener('click', () => {
      clearError();
      refreshRegistry(key, b.dataset.tab);
    }));
    try {
      const meta = await http.get(`/api/profiles/${encSeg(key)}`);
      renderMeta(meta);
    } catch (e) { showError(e.message); metaPanel.innerHTML = '<h3>…</h3>'; }
    refreshRegistry(key, 'ench');
  };

  el.appendChild(list);
  el.appendChild(metaPanel);
  el.appendChild(tabs);
  el.appendChild(regEl);
  load().then(async (active) => {
    current = active;
    await selectProfile(active);
  }).catch((e) => showError(e.message));
}
