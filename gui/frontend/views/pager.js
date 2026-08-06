// pager.js — generic local pagination helpers (pure functions + binding).
// Shared by registry views: slice an array per page and render a small
// prev/next control. All HTML goes through esc(); keys pager.* (en/zh).
import { esc } from '../api.js';
import { t, tf } from '../i18n.js';

// Slice `items` to one page. `page` is clamped into [1, total] (empty input
// → page 1 / total 0). Returns { page, total, items }.
export function paginate(items, page, pageSize) {
  const n = (items || []).length;
  const total = n === 0 ? 0 : Math.ceil(n / pageSize);
  const cur = Math.min(Math.max(1, Math.floor(page) || 1), Math.max(1, total));
  const start = (cur - 1) * pageSize;
  return { page: cur, total, items: items.slice(start, start + pageSize) };
}

// Pager control HTML ('' when there is nothing to page through). The
// buttons carry data-pg target pages; bindPager wires them.
export function pagerHtml(page, total) {
  if (total <= 1) return '';
  return `<div class="pager">` +
    `<button type="button" class="pager-btn" data-pg="${page - 1}"` +
    `${page <= 1 ? ' disabled' : ''}>${esc(t('pager.prev'))}</button>` +
    `<span class="pager-info">${esc(tf('pager.page', page, total))}</span>` +
    `<button type="button" class="pager-btn" data-pg="${page + 1}"` +
    `${page >= total ? ' disabled' : ''}>${esc(t('pager.next'))}</button></div>`;
}

// Wire one pager inside `container`; `onPage(pg)` re-renders the page
// (the container is expected to be rebuilt by the caller's render path).
export function bindPager(container, onPage) {
  container.querySelectorAll('.pager-btn').forEach((b) => b.addEventListener('click', () => {
    const pg = Number(b.dataset.pg);
    if (pg >= 1) onPage(pg);
  }));
}
