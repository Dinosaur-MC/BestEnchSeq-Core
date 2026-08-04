// profiles.js — Profiles view against the resource-ized contract:
//   /api/profiles, /api/profiles/{key}[+ /activate /rename], and the per-key
//   sub-resources /enchantments|equipments|tags (+/{name}).
// Renders list/activate/fork-create/remove, a metadata panel (GET /api/profiles/
// {key}), and registry tables with add / remove / PATCH-edit. Equipment rows
// carry an item icon from /public (hidden on 404).
import { http, showError, clearError, esc } from '../api.js';
import { t } from '../i18n.js';

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

// "minecraft:netherite_sword" → "netherite_sword" for the /public icon file.
function itemIdToFile(id) {
  const s = String(id || '');
  return s.startsWith('minecraft:') ? s.slice('minecraft:'.length) : s;
}

// A minimal entry the backend's validation accepts. The en/equip/tag fields the
// registry requires differ — this posts just enough to create + list an entry
// (the GUI has no full-form editor in v1). Vanilla-free: `category` is omitted
// (an empty string would fail NSID validation; the field is optional) and
// `supported_items` is an empty set.
function minimalEntry(kind, id) {
  if (kind === 'equip') return { id, name: id, max_durability: 100 };
  if (kind === 'tag') return { id, name: id };
  return { id, name: id, max_level: 1, multiplier: 1, supported_items: [] };
}

// Inline JSON editor for a single registry entry — the PATCH editor.
// `entry` is the full entry object (from the list response); saving PATCHes it
// back to /api/profiles/{key}/{plural}/{id} and re-renders.
function openEditor(el, key, kind, entry) {
  const card = document.createElement('div');
  card.className = 'card';
  card.style.margin = '8px 0';
  const ta = document.createElement('textarea');
  ta.className = 'mono';
  ta.value = JSON.stringify(entry, null, 2);
  ta.style.width = '100%';
  ta.style.minHeight = '120px';
  const row = document.createElement('div');
  const save = document.createElement('button');
  save.textContent = 'Save';
  const cancel = document.createElement('button');
  cancel.textContent = 'Cancel';
  cancel.className = 'secondary';
  save.addEventListener('click', async () => {
    clearError();
    let patch;
    try { patch = JSON.parse(ta.value); } catch (e) { showError('Invalid JSON'); return; }
    try {
      await http.patch(`/api/profiles/${encSeg(key)}/${KINDS[kind].plural}/${encSeg(entry.id)}`, patch);
      card.remove();
      await renderRegistry(el, key, kind);
    } catch (e) { showError(e.message); }
  });
  cancel.addEventListener('click', () => card.remove());
  row.appendChild(save);
  row.appendChild(cancel);
  card.appendChild(ta);
  card.appendChild(row);
  el.appendChild(card);
  ta.focus();
}

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
  const icon = kind === 'equip'
    ? (id) => `<img src="/public/${itemIdToFile(id)}.png" alt="" style="width:20px;height:20px;vertical-align:middle;margin-right:4px" onerror="this.style.display='none'">`
    : () => '';
  const rows = entries
    .map((e) => `<tr>
        <td>${icon(e.id)}${esc(e.id)}</td>
        <td>${esc(e.name || '')}</td>
        <td>${esc(e.max_level ?? e.max_durability ?? '')}</td>
        <td>
          <button data-edit="${esc(e.id)}">Edit</button>
          <button data-rm="${esc(e.id)}">${t('prof.remove')}</button>
        </td></tr>`)
    .join('');
  wrap.innerHTML = `
    <h3>${t('prof.' + kind)}</h3>
    <table><thead><tr><th>${t('prof.id')}</th><th>${t('prof.name')}</th><th>${t('prof.max_level')}</th><th></th></tr></thead>
    <tbody>${rows || '<tr><td colspan="4">—</td></tr>'}</tbody></table>
    <label>${t('prof.id')}</label><input class="add-id">
    <button class="add-row">${t('prof.add')}</button>`;
  wrap.querySelector('.add-row').addEventListener('click', async () => {
    clearError();
    const id = wrap.querySelector('.add-id').value.trim();
    if (!id) return;
    try {
      await http.post(`/api/profiles/${encSeg(profile)}/${plural}`, minimalEntry(kind, id));
      await renderRegistry(el, profile, kind);
    } catch (e) { showError(e.message); }
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
    if (entry) openEditor(el, profile, kind, entry);
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
        <button class="deps-save">Save</button></td></tr>
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
    list.innerHTML = `<table><thead><tr><th>${t('prof.name')}</th><th></th><th></th></tr></thead><tbody>` +
      data.profiles.map((p) => `<tr><td>${esc(p)}${p === data.active ? ` (${t('prof.active')})` : ''}</td>
        <td><button data-act="${esc(p)}">${t('prof.activate')}</button>
            <button data-view="${esc(p)}">View</button></td>
        <td><button data-ren="${esc(p)}">Rename</button>
            <button data-rmp="${esc(p)}">${t('prof.remove')}</button></td></tr>`).join('') +
      `</tbody></table>
       <label>${t('prof.new_name')}</label><input class="fork-name">
       <button class="fork-btn">${t('prof.create')}</button>`;

    list.querySelectorAll('[data-act]').forEach((b) => b.addEventListener('click', async () => {
      clearError();
      const key = b.dataset.act;
      try {
        await http.post(`/api/profiles/${encSeg(key)}/activate`);
        current = key;
        await load();
        await selectProfile(key);
      } catch (e) { showError(e.message); }
    }));
    list.querySelectorAll('[data-view]').forEach((b) => b.addEventListener('click', async () => {
      clearError();
      const key = b.dataset.view;
      current = key;
      await selectProfile(key);
    }));
    list.querySelectorAll('[data-rmp]').forEach((b) => b.addEventListener('click', async () => {
      clearError();
      const key = b.dataset.rmp;
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
      if (/[/#?%]/.test(name)) { showError('Invalid profile name'); return; }
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
      if (/[/#?%]/.test(name)) { showError('Invalid profile name'); return; }
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
