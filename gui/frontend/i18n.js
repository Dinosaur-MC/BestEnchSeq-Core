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
    'prof.title': 'Profiles', 'prof.active': 'Active',
    'prof.new_name': 'New profile name', 'prof.fork_from': 'Fork from',
    'prof.create': 'Create', 'prof.activate': 'Activate', 'prof.remove': 'Remove',
    'prof.publish': 'Publish', 'prof.ench': 'Enchantments', 'prof.equip': 'Equipment', 'prof.tag': 'Tags',
    'prof.add': 'Add', 'prof.id': 'ID', 'prof.name': 'Name', 'prof.max_level': 'Max level',
    'alg.title': 'Algorithms', 'alg.load_dir': 'Plugin directory',
    'alg.load': 'Load', 'alg.loaded': 'Loaded',
    'logs.title': 'Logs', 'logs.tail': 'Tail', 'logs.level': 'Level',
    'logs.refresh': 'Refresh', 'logs.auto': 'Auto-refresh',
    'set.title': 'Settings', 'set.lang': 'Language', 'set.log_level': 'Log level',
    'set.save': 'Save', 'set.saved': 'Saved',
    'status.title': 'Status', 'status.profile': 'Active profile',
    'status.algorithms': 'Algorithms', 'status.solve': 'Active solve', 'status.uptime': 'Uptime (ms)',
    'status.solve_yes': 'Yes', 'status.solve_no': 'No',
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
    'prof.title': '数据', 'prof.active': '当前',
    'prof.new_name': '新数据名', 'prof.fork_from': '分支来源',
    'prof.create': '创建', 'prof.activate': '启用', 'prof.remove': '删除',
    'prof.publish': '发布', 'prof.ench': '魔咒', 'prof.equip': '装备', 'prof.tag': '标签',
    'prof.add': '添加', 'prof.id': 'ID', 'prof.name': '名称', 'prof.max_level': '最大等级',
    'alg.title': '算法', 'alg.load_dir': '插件目录', 'alg.load': '加载', 'alg.loaded': '已加载',
    'logs.title': '日志', 'logs.tail': '条数', 'logs.level': '级别', 'logs.refresh': '刷新', 'logs.auto': '自动刷新',
    'set.title': '设置', 'set.lang': '语言', 'set.log_level': '日志级别', 'set.save': '保存', 'set.saved': '已保存',
    'status.title': '状态', 'status.profile': '当前数据', 'status.algorithms': '算法数', 'status.solve': '求解中', 'status.uptime': '运行时长（毫秒）',
    'status.solve_yes': '是', 'status.solve_no': '否',
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
