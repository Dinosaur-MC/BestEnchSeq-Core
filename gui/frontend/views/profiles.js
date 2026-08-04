// profiles.js — Profiles view: list/activate/fork-create/remove profiles and
// read/add/remove registry entries (ench | equip | tag) for the active profile.
import { http, showError, clearError, esc } from '../api.js';
import { t } from '../i18n.js';

let renderSeq = 0; // guards registry renders against overlapping async re-renders

// `data-rm` holds "kind:name". The name is an NSID and may itself contain ':'
// (e.g. minecraft:sharpness, #minecraft:swords), so split on the FIRST colon
// only — `arr.split(':')` destructuring would drop everything past the second.
function splitRm(value) {
  const sep = value.indexOf(':');
  return [value.slice(0, sep), value.slice(sep + 1)];
}

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

// Render one registry (kind ∈ ench|equip|tag) for `profile` into `el`, which
// must be a dedicated container: the card replaces whatever was there so the
// add/remove re-render never duplicates it.
async function renderRegistry(el, profile, kind) {
  const seq = ++renderSeq;
  el.replaceChildren();
  const wrap = document.createElement('div');
  wrap.className = 'card';
  const data = await http.get(`/api/profile/${encSeg(profile)}/${kind}`);
  if (seq !== renderSeq) return; // a newer render superseded this one — don't append stale DOM
  const key = kind === 'ench' ? 'enchantments' : kind === 'equip' ? 'equipments' : 'tags';
  const rows = (data[key] || [])
    .map((e) => `<tr><td>${esc(e.id)}</td><td>${esc(e.name || '')}</td><td>${esc(e.max_level ?? e.max_durability ?? '')}</td>
        <td><button data-rm="${kind}:${esc(e.id)}">${t('prof.remove')}</button></td></tr>`)
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
      await http.post(`/api/profile/${encSeg(profile)}/${kind}`, minimalEntry(kind, id));
      await renderRegistry(el, profile, kind);
    } catch (e) { showError(e.message); }
  });
  wrap.querySelectorAll('[data-rm]').forEach((b) => b.addEventListener('click', async () => {
    clearError();
    const [k, name] = splitRm(b.dataset.rm);
    try {
      await http.del(`/api/profile/${encSeg(profile)}/${k}/${encSeg(name)}`);
      await renderRegistry(el, profile, kind);
    } catch (e) { showError(e.message); }
  }));
  el.appendChild(wrap);
}

export function render(el) {
  el.innerHTML = `<h2>${t('prof.title')}</h2>`;
  const list = document.createElement('div');
  list.className = 'card';
  const regEl = document.createElement('div');
  const load = async () => {
    const data = await http.get('/api/profile');
    list.innerHTML = `<table><thead><tr><th>${t('prof.name')}</th><th></th><th></th></tr></thead><tbody>` +
      data.profiles.map((p) => `<tr><td>${esc(p)}${p === data.active ? ` (${t('prof.active')})` : ''}</td>
        <td><button data-act="${esc(p)}">${t('prof.activate')}</button></td>
        <td><button data-rmp="${esc(p)}">${t('prof.remove')}</button></td></tr>`).join('') +
      `</tbody></table>
       <label>${t('prof.new_name')}</label><input class="fork-name">
       <button class="fork-btn">${t('prof.create')}</button>`;
    list.querySelectorAll('[data-act]').forEach((b) => b.addEventListener('click', async () => {
      clearError();
      try {
        await http.post('/api/profile', { action: 'activate', name: b.dataset.act });
        const active = await load();
        await renderRegistry(regEl, active, 'ench');
      } catch (e) { showError(e.message); }
    }));
    list.querySelectorAll('[data-rmp]').forEach((b) => b.addEventListener('click', async () => {
      clearError();
      try {
        await http.post('/api/profile', { action: 'remove', name: b.dataset.rmp });
        const active = await load();
        await renderRegistry(regEl, active, 'ench');
      } catch (e) { showError(e.message); }
    }));
    list.querySelector('.fork-btn').addEventListener('click', async () => {
      clearError();
      const name = list.querySelector('.fork-name').value.trim();
      if (!name) return;
      // URL-hostile characters are rejected at the input so a created profile is
      // always addressable by the registry routes below.
      if (/[/#?%]/.test(name)) { showError('Invalid profile name'); return; }
      try {
        await http.post('/api/profile', { action: 'fork', source: data.active, dest: name });
        await load();
      } catch (e) { showError(e.message); }
    });
    return data.active;
  };
  el.appendChild(list);
  el.appendChild(regEl);
  load().then(async (active) => {
    await renderRegistry(regEl, active, 'ench');
  }).catch((e) => showError(e.message));
}
