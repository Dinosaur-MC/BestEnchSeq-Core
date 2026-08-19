#include "domain/business/sql/SqlExecutor.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/types/Profile.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace business::sql {

namespace {

// ── 列元数据：名称 + 类型（ORDER BY 数值列按 int 比较） ──────────────
enum class ColKind { Str, Int, Bool, List };

struct ColDef {
    std::string_view name;
    ColKind kind;
};

const std::vector<ColDef>& enchantment_cols() {
    static const std::vector<ColDef> cols = {
        {"id", ColKind::Str},
        {"name", ColKind::Str},
        {"supported_platform", ColKind::Str},
        {"max_level", ColKind::Int},
        {"limited_level", ColKind::Int},
        {"multiplier", ColKind::Int},
        {"is_treasure", ColKind::Bool},
        {"exclusive_set", ColKind::List},
        {"supported_items", ColKind::List},
        {"min_cost_base", ColKind::Int},
        {"min_cost_per_level", ColKind::Int},
    };
    return cols;
}

const std::vector<ColDef>& equipment_cols() {
    static const std::vector<ColDef> cols = {
        {"id", ColKind::Str},
        {"name", ColKind::Str},
        {"category", ColKind::Str},
        {"max_durability", ColKind::Int},
    };
    return cols;
}

const std::vector<ColDef>& tags_cols() {
    static const std::vector<ColDef> cols = {
        {"id", ColKind::Str},
        {"name", ColKind::Str},
        {"values", ColKind::List},
    };
    return cols;
}

const std::vector<ColDef>* table_cols(const std::string& table) {
    if (table == "enchantment")
        return &enchantment_cols();
    if (table == "equipment")
        return &equipment_cols();
    if (table == "tags")
        return &tags_cols();
    return nullptr;
}

// MCE → 数据表示字符串（与 Serializer::mce_to_string 一致：none/java/bedrock/all）
std::string_view mce_str(MCE p) {
    switch (p) {
    case MCE::Java:
        return "java";
    case MCE::Bedrock:
        return "bedrock";
    case MCE::All:
        return "all";
    default:
        return "none";
    }
}

/// NSID 集合 → 逗号拼接（排序后），空集合 → ""。
std::string join_sorted(const std::unordered_set<NSID>& set) {
    std::vector<std::string> items;
    items.reserve(set.size());
    for (const auto& id : set)
        items.push_back(id.str());
    std::sort(items.begin(), items.end());
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i)
            out += ',';
        out += items[i];
    }
    return out;
}

std::string join_sorted_str(const std::unordered_set<std::string>& set) {
    std::vector<std::string> items(set.begin(), set.end());
    std::sort(items.begin(), items.end());
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i)
            out += ',';
        out += items[i];
    }
    return out;
}

/// 标签的 values 列：经 profile 的 TagResolver 解析成员（排序后逗号拼接）。
/// 无 resolver / 标签未定义 → ""。
std::string tag_values(const Profile& p, const EquipmentTag& t) {
    const TagResolver* tr = p.tag_resolver();
    if (!tr)
        return "";
    if (const auto* vals = tr->get_tag(t.id.get_ns(), t.id.get_id()))
        return join_sorted_str(*vals);
    return "";
}

// ── 行构造：按表列定义顺序生成全列字符串化行 ─────────────────────────

std::vector<std::string> enchantment_row(const EnchInfo& e) {
    return {
        e.id.str(),
        e.name,
        std::string(mce_str(e.supported_platform)),
        std::to_string(e.max_level),
        std::to_string(e.limited_level),
        std::to_string(e.multiplier),
        e.is_treasure ? "true" : "false",
        join_sorted(e.exclusive_set),
        join_sorted(e.supported_items),
        std::to_string(e.min_cost_base),
        std::to_string(e.min_cost_per_level),
    };
}

std::vector<std::string> equipment_row(const Equipment& eq) {
    return {eq.id.str(), eq.name, eq.category.str(), std::to_string(eq.max_durability)};
}

std::vector<std::string> tags_row(const Profile& p, const EquipmentTag& t) {
    return {t.id.str(), t.name, tag_values(p, t)};
}

int64_t to_int(const std::string& s) {
    try {
        return std::stoll(s);
    } catch (...) {
        return 0;
    }
}

} // namespace

SqlExecutor::SqlExecutor(ProfileManager& mgr, std::string profiles_dir) : _mgr(mgr), _profiles_dir(std::move(profiles_dir)) {}

void SqlExecutor::set_current(std::string profile) {
    _current = std::move(profile);
}

const std::string& SqlExecutor::current() const {
    return _current;
}

SqlResult SqlExecutor::execute(const SqlStmt& stmt) {
    if (const auto* sel = std::get_if<SelectStmt>(&stmt))
        return exec_select(*sel);
    // 写面在 Task 3：本执行器返回错误结果，不产生副作用。
    SqlResult r;
    r.message = "write statements (INSERT/UPDATE/DELETE/STATUS/SAVE) land in Task 3";
    return r;
}

SqlResult SqlExecutor::exec_select(const SelectStmt& s) {
    SqlResult out;

    const std::vector<ColDef>* cols = table_cols(s.table);
    if (!cols) {
        out.message = "unknown table '" + s.table + "'";
        return out;
    }

    const Profile* prof = _mgr.find(_current);
    if (!prof) {
        out.message = "unknown profile '" + _current + "'";
        return out;
    }

    auto col_index = [&](const std::string& name) -> int {
        for (size_t i = 0; i < cols->size(); ++i)
            if ((*cols)[i].name == name)
                return static_cast<int>(i);
        return -1;
    };

    // 列投影：star/SHOW → 全列；显式列 → 校验存在性（未知列报错）。
    std::vector<size_t> proj;
    if (s.star) {
        for (size_t i = 0; i < cols->size(); ++i)
            proj.push_back(i);
    } else {
        for (const auto& c : s.cols) {
            const int idx = col_index(c);
            if (idx < 0) {
                out.message = "unknown column '" + c + "'";
                return out;
            }
            proj.push_back(static_cast<size_t>(idx));
        }
    }

    // WHERE / ORDER BY 列校验。
    for (const auto& w : s.where) {
        if (w.col.empty())
            continue; // 哨兵
        if (col_index(w.col) < 0) {
            out.message = "unknown column '" + w.col + "' in WHERE";
            return out;
        }
    }
    if (!s.order_by.empty() && col_index(s.order_by) < 0) {
        out.message = "unknown column '" + s.order_by + "' in ORDER BY";
        return out;
    }

    // 物化全列行。
    std::vector<std::vector<std::string>> rows;
    if (s.table == "enchantment") {
        for (const auto& e : prof->ench())
            rows.push_back(enchantment_row(e));
    } else if (s.table == "equipment") {
        for (const auto& eq : prof->eq())
            rows.push_back(equipment_row(eq));
    } else { // tags
        for (const auto& t : prof->tags())
            rows.push_back(tags_row(*prof, t));
    }

    // WHERE 过滤（AND 语义）：哨兵（col 空，val=="true"）匹配全部；
    // 常规条件比较字符串化列值 == cond.val。
    if (!s.where.empty()) {
        std::vector<std::vector<std::string>> filtered;
        for (const auto& row : rows) {
            bool match = true;
            for (const auto& w : s.where) {
                if (w.col.empty())
                    continue; // 哨兵：WHERE true 匹配全部
                const size_t idx = static_cast<size_t>(col_index(w.col));
                if (row[idx] != w.val) {
                    match = false;
                    break;
                }
            }
            if (match)
                filtered.push_back(row);
        }
        rows = std::move(filtered);
    }

    // ORDER BY（单列，asc/desc；数值列按 int 比较，其余按字符串）。
    if (!s.order_by.empty()) {
        const size_t oi = static_cast<size_t>(col_index(s.order_by));
        const bool numeric = (*cols)[oi].kind == ColKind::Int;
        std::stable_sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) {
            int cmp = 0;
            if (numeric) {
                const int64_t av = to_int(a[oi]), bv = to_int(b[oi]);
                cmp = av < bv ? -1 : (av > bv ? 1 : 0);
            } else {
                cmp = a[oi].compare(b[oi]);
            }
            return s.desc ? cmp > 0 : cmp < 0;
        });
    }

    // LIMIT / OFFSET（ORDER 之后应用）。
    const int64_t total = static_cast<int64_t>(rows.size());
    const int64_t start = s.offset > 0 ? s.offset : 0;
    int64_t end = total;
    if (s.limit >= 0 && start + s.limit < end)
        end = start + s.limit;

    // 投影 + 组装结果。
    for (size_t p : proj)
        out.headers.push_back(std::string((*cols)[p].name));
    out.rows.reserve(static_cast<size_t>(std::max<int64_t>(0, end - start)));
    for (int64_t i = start; i < end && i < total; ++i) {
        std::vector<std::string> row;
        row.reserve(proj.size());
        for (size_t p : proj)
            row.push_back(rows[static_cast<size_t>(i)][p]);
        out.rows.push_back(std::move(row));
    }
    return out;
}

} // namespace business::sql
