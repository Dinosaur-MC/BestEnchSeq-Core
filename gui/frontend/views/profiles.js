// profiles.js — Profiles view: list/activate/fork-create/remove profiles and
// read/add/remove registry entries (ench | equip | tag) for the active profile.
import { http, showError, esc } from '../api.js';
import { t } from '../i18n.js';

// `data-rm` holds "kind:name". The name is an NSID and may itself contain ':'
// (e.g. minecraft:sharpness, #minecraft:swords), so split on the FIRST colon
// only — `arr.split(':')` destructuring would drop everything past the second.
function splitRm(value) {
  const sep = value.indexOf(':');
  return [value.slice(0, sep), value.slice(sep + 1)];
}

// A minimal entry the backend's validation accepts. The en/equip/tag fields
// the registry requires differ — this posts just enough to create + list an
// entry (the GUI has no full-form editor in v1).
function minimalEntry(kind, id) {
  if (kind === 'equip') return { id, name: id, category: '#minecraft:sword', max_durability: 100 };
  if (kind === 'tag') return { id, name: id };
  return { id, name: id, max_level: 1, multiplier: 1, supported_items: ['#minecraft:swords'] };
}

// Render one registry (kind ∈ ench|equip|tag) for `profile` into `el`, which
// must be a dedicated container: the card replaces whatever was there so the
// add/remove re-render never duplicates it.
async function renderRegistry(el, profile, kind) {
  el.replaceChildren();
  const wrap = document.createElement('div');
  wrap.className = 'card';
  const data = await http.get(`/api/profile/${profile}/${kind}`);
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
    const id = wrap.querySelector('.add-id').value.trim();
    if (!id) return;
    try {
      await http.post(`/api/profile/${profile}/${kind}`, minimalEntry(kind, id));
      await renderRegistry(el, profile, kind);
    } catch (e) { showError(e.message); }
  });
  wrap.querySelectorAll('[data-rm]').forEach((b) => b.addEventListener('click', async () => {
    const [k, name] = splitRm(b.dataset.rm);
    try {
      await http.del(`/api/profile/${profile}/${k}/${name}`);
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
      try {
        await http.post('/api/profile', { action: 'activate', name: b.dataset.act });
        const active = await load();
        await renderRegistry(regEl, active, 'ench');
      } catch (e) { showError(e.message); }
    }));
    list.querySelectorAll('[data-rmp]').forEach((b) => b.addEventListener('click', async () => {
      try {
        await http.post('/api/profile', { action: 'remove', name: b.dataset.rmp });
        const active = await load();
        await renderRegistry(regEl, active, 'ench');
      } catch (e) { showError(e.message); }
    }));
    list.querySelector('.fork-btn').addEventListener('click', async () => {
      const name = list.querySelector('.fork-name').value.trim();
      if (!name) return;
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
