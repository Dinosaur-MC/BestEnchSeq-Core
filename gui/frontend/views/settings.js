// settings.js — Settings view: edit language + log settings, persist via
// PATCH (the backend also writes <cwd>/config.json so the values survive a
// restart).  The service group is read-only info determined at startup.
import { http, showError, clearError, esc } from '../api.js';
import { t, setLang, langCode, applyI18n } from '../i18n.js';

const LEVELS = [0, 1, 2, 3];
const LEVEL_KEYS = ['level.debug', 'level.info', 'level.warn', 'level.error'];

// Editable row: label + control (select / mdui-switch).
function editableRow(label, controlHtml) {
  return `<div class="set-row"><span class="set-label">${label}</span>${controlHtml}</div>`;
}

// Read-only key-value row (service group).
function roRow(label, valueHtml) {
  return `<div class="set-ro"><span>${label}</span><span>${valueHtml}</span></div>`;
}

function levelSelect(id, current) {
  return `<select id="${id}">${LEVELS.map((lv) =>
    `<option value="${lv}" ${current === lv ? 'selected' : ''}>${t(LEVEL_KEYS[lv])}</option>`)
    .join('')}</select>`;
}

export async function render(el) {
  const myView = el.dataset.view; // bail if route() re-stamps ownership mid-await
  const s = await http.get('/api/settings');
  if (el.dataset.view !== myView) return;
  const langSel = `<select id="set-lang">
      <option value="en_US" ${s.lang === 'en_US' ? 'selected' : ''}>English</option>
      <option value="zh_CN" ${s.lang === 'zh_CN' ? 'selected' : ''}>中文</option>
    </select>`;
  el.innerHTML = `<h2>${t('set.title')}</h2>
    <div class="card set-group">
      <h3>${t('set.group_lang')}</h3>
      ${editableRow(t('set.lang'), langSel)}
    </div>
    <div class="card set-group">
      <h3>${t('set.group_log')}</h3>
      ${editableRow(t('set.log_level'), levelSelect('set-loglevel', s.log_level))}
      ${editableRow(t('set.log_retention'),
        `<input type="number" id="set-log-retention" min="0" step="1" value="${s.log_retention}">`)}
      ${editableRow(t('set.log_console'),
        `<mdui-switch id="set-log-console" ${s.log_console ? 'checked' : ''}></mdui-switch>`)}
      ${editableRow(t('set.log_console_level'), levelSelect('set-console-level', s.log_console_level))}
    </div>
    <div class="card set-group">
      <h3>${t('set.group_service')} <span class="set-ro-hint">${t('set.readonly')}</span></h3>
      ${roRow(t('set.gui_host'), esc(s.gui_host))}
      ${roRow(t('set.gui_port'), String(s.gui_port))}
      ${roRow(t('set.gui_open_browser'), s.gui_open_browser ? t('set.on') : t('set.off'))}
      ${roRow(t('set.gui_workers'), String(s.gui_workers))}
      ${roRow(t('set.memory_mb'), String(s.memory_mb))}
      ${roRow(t('set.sandbox_enabled'), s.sandbox_enabled ? t('set.on') : t('set.off'))}
      ${roRow(t('set.log_dir'), esc(s.log_dir))}
      ${roRow(t('set.algo_dir'), esc(s.algo_dir))}
      ${roRow(t('set.state_dir'), esc(s.state_dir))}
      ${roRow(t('set.state_autosave'), s.state_autosave ? t('set.on') : t('set.off'))}
    </div>
    <div class="set-actions">
      <button id="set-save">${t('set.save')}</button>
      <span id="set-msg" class="set-msg"></span>
    </div>`;
  document.getElementById('set-save').addEventListener('click', async () => {
    clearError();
    try {
      const body = {
        lang: document.getElementById('set-lang').value,
        log_level: parseInt(document.getElementById('set-loglevel').value, 10),
        log_retention: parseInt(document.getElementById('set-log-retention').value, 10),
        log_console: document.getElementById('set-log-console').checked,
        log_console_level: parseInt(document.getElementById('set-console-level').value, 10),
      };
      await http.patch('/api/settings', body);
      if (el.dataset.view !== myView) return; // save finished after navigation
      const langChanged = body.lang !== (langCode() === 'zh-CN' ? 'zh_CN' : 'en_US');
      if (langChanged) {
        setLang(body.lang === 'zh_CN' ? 'zh-CN' : 'en-US');
        applyI18n();
        // Re-render the current view so its t() strings pick up the new language.
        window.dispatchEvent(new HashChangeEvent('hashchange'));
      } else {
        // No language switch → the view stays; show the saved note.
        document.getElementById('set-msg').textContent = t('set.saved');
      }
    } catch (e) { showError(e.code, e.message); }
  });
}
