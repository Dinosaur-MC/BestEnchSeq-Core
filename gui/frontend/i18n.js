// Minimal frontend dictionary; backend errors come pre-localized.
const dicts = {
  'en-US': {
    'nav.calculator': 'Calculator', 'nav.profiles': 'Profiles',
    'nav.algorithms': 'Algorithms', 'nav.logs': 'Logs',
    'nav.settings': 'Settings', 'nav.status': 'Status',
    'calc.title': 'Enchantment Calculator',
    'calc.target_item': 'Target item', 'calc.target_enchants': 'Target enchantments',
    'calc.source': 'Starting enchantments (optional)',
    'calc.algorithm': 'Algorithm',
    'calc.solutions': 'Solutions',
    'calc.step': 'Step', 'calc.target': 'target', 'calc.book': 'book',
    'calc.cost': 'Level cost', 'calc.total_cost': 'Total cost',
    'calc.final_item': 'Final item',
    'calc.unreachable': 'Target unreachable',
    'calc.no_result': 'No result',
    'calc.run': 'Solve', 'calc.cancel': 'Cancel',
    'calc.progress': 'Solving…',
    'err.network': 'Network error — is besq-gui running?',
  },
  'zh-CN': {
    'nav.calculator': '计算器', 'nav.profiles': '数据',
    'nav.algorithms': '算法', 'nav.logs': '日志',
    'nav.settings': '设置', 'nav.status': '状态',
    'calc.title': '附魔计算器',
    'calc.target_item': '目标装备', 'calc.target_enchants': '目标魔咒',
    'calc.source': '起点魔咒（可选）',
    'calc.algorithm': '算法',
    'calc.solutions': '方案数',
    'calc.step': '步骤', 'calc.target': '目标', 'calc.book': '书',
    'calc.cost': '等级成本', 'calc.total_cost': '总成本',
    'calc.final_item': '最终物品',
    'calc.unreachable': '目标不可达',
    'calc.no_result': '无结果',
    'calc.run': '求解', 'calc.cancel': '取消',
    'calc.progress': '求解中…',
    'err.network': '网络错误 — besq-gui 未运行？',
  },
};

let lang = 'en-US';
export function setLang(l) { if (dicts[l]) lang = l; }
export function t(key) { return (dicts[lang] && dicts[lang][key]) || key; }

// Apply data-i18n attributes and re-render on language change.
export function applyI18n(root) {
  (root || document).querySelectorAll('[data-i18n]').forEach((el) => {
    el.textContent = t(el.dataset.i18n);
  });
}
