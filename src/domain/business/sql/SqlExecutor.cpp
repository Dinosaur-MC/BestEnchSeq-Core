#include "domain/business/sql/SqlExecutor.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/types/Profile.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
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

int col_index(const std::vector<ColDef>& cols, const std::string& name) {
    for (size_t i = 0; i < cols.size(); ++i)
        if (cols[i].name == name)
            return static_cast<int>(i);
    return -1;
}

// ── 写面绑定辅助 ───────────────────────────────────────────────────────

/// 列表列：按逗号拆分，逐项去空白，丢弃空项。
std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        const std::string item = s.substr(start, comma == std::string::npos ? s.size() - start : comma - start);
        const size_t b = item.find_first_not_of(" \t\r\n");
        if (b != std::string::npos) {
            const size_t e = item.find_last_not_of(" \t\r\n");
            out.push_back(item.substr(b, e - b + 1));
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return out;
}

bool parse_int32(const std::string& s, int32_t& out) {
    if (s.empty())
        return false;
    size_t pos = 0;
    try {
        const long long v = std::stoll(s, &pos);
        if (pos != s.size() || v < std::numeric_limits<int32_t>::min() || v > std::numeric_limits<int32_t>::max())
            return false;
        out = static_cast<int32_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_bool(const std::string& s, bool& out) {
    if (s == "true") {
        out = true;
        return true;
    }
    if (s == "false") {
        out = false;
        return true;
    }
    return false;
}

/// 严格平台解析（未知值 → false；与 Serializer 读侧宽松回退不同，写侧报错）。
bool parse_mce_strict(const std::string& s, MCE& out) {
    if (s == "none") {
        out = MCE::None;
        return true;
    }
    if (s == "java") {
        out = MCE::Java;
        return true;
    }
    if (s == "bedrock") {
        out = MCE::Bedrock;
        return true;
    }
    if (s == "all") {
        out = MCE::All;
        return true;
    }
    return false;
}

/// NSID 构造（非法输入抛 runtime_error → 捕获转 false）。
bool to_nsid(const std::string& s, NSID& out) {
    try {
        out = NSID(s);
        return true;
    } catch (...) {
        return false;
    }
}

/// 列表列 → NSID 集合；任一非法项 → false + err。
bool nsid_set_from_list(const std::string& raw, std::unordered_set<NSID>& out, std::string& err) {
    for (const auto& item : split_list(raw)) {
        NSID id;
        if (!to_nsid(item, id)) {
            err = "invalid NSID '" + item + "'";
            return false;
        }
        out.insert(std::move(id));
    }
    return true;
}

std::string join_err(const std::vector<std::string>& items, const char* sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i)
            out += sep;
        out += items[i];
    }
    return out;
}

std::string quoted_join(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i)
            out += ", ";
        out += "'" + items[i] + "'";
    }
    return out;
}

// ── 列 → 字段绑定（INSERT 全列 / UPDATE SET 单列共用） ─────────────────

bool set_field_enchantment(const std::string& col, const std::string& val, EnchInfo& e, std::string& err) {
    if (col == "id") {
        if (!to_nsid(val, e.id)) {
            err = "invalid NSID '" + val + "' for column 'id'";
            return false;
        }
        return true;
    }
    if (col == "name") {
        e.name = val;
        return true;
    }
    if (col == "supported_platform") {
        if (!parse_mce_strict(val, e.supported_platform)) {
            err = "invalid supported_platform '" + val + "'";
            return false;
        }
        return true;
    }
    if (col == "max_level") {
        if (!parse_int32(val, e.max_level)) {
            err = "invalid integer '" + val + "' for column 'max_level'";
            return false;
        }
        return true;
    }
    if (col == "limited_level") {
        if (!parse_int32(val, e.limited_level)) {
            err = "invalid integer '" + val + "' for column 'limited_level'";
            return false;
        }
        return true;
    }
    if (col == "multiplier") {
        if (!parse_int32(val, e.multiplier)) {
            err = "invalid integer '" + val + "' for column 'multiplier'";
            return false;
        }
        return true;
    }
    if (col == "is_treasure") {
        bool b = false;
        if (!parse_bool(val, b)) {
            err = "invalid boolean '" + val + "' for column 'is_treasure'";
            return false;
        }
        e.is_treasure = b;
        return true;
    }
    if (col == "exclusive_set") {
        // SET 语义 = 整列替换（patch 是既有行拷贝，须赋新集而非并入）。
        std::unordered_set<NSID> parsed;
        if (!nsid_set_from_list(val, parsed, err))
            return false;
        e.exclusive_set = std::move(parsed);
        return true;
    }
    if (col == "supported_items") {
        std::unordered_set<NSID> parsed;
        if (!nsid_set_from_list(val, parsed, err))
            return false;
        e.supported_items = std::move(parsed);
        return true;
    }
    if (col == "min_cost_base") {
        if (!parse_int32(val, e.min_cost_base)) {
            err = "invalid integer '" + val + "' for column 'min_cost_base'";
            return false;
        }
        return true;
    }
    if (col == "min_cost_per_level") {
        if (!parse_int32(val, e.min_cost_per_level)) {
            err = "invalid integer '" + val + "' for column 'min_cost_per_level'";
            return false;
        }
        return true;
    }
    err = "unknown column '" + col + "'";
    return false;
}

bool set_field_equipment(const std::string& col, const std::string& val, Equipment& eq, std::string& err) {
    if (col == "id") {
        if (!to_nsid(val, eq.id)) {
            err = "invalid NSID '" + val + "' for column 'id'";
            return false;
        }
        return true;
    }
    if (col == "name") {
        eq.name = val;
        return true;
    }
    if (col == "category") {
        if (!to_nsid(val, eq.category)) {
            err = "invalid NSID '" + val + "' for column 'category'";
            return false;
        }
        return true;
    }
    if (col == "max_durability") {
        if (!parse_int32(val, eq.max_durability)) {
            err = "invalid integer '" + val + "' for column 'max_durability'";
            return false;
        }
        return true;
    }
    err = "unknown column '" + col + "'";
    return false;
}

/// tags 行：values 列不落 EquipmentTag（其无该字段），写入独立 values 向量。
bool set_field_tags(
    const std::string& col, const std::string& val, EquipmentTag& t, std::vector<std::string>& values, std::string& err) {
    if (col == "id") {
        if (!to_nsid(val, t.id)) {
            err = "invalid NSID '" + val + "' for column 'id'";
            return false;
        }
        return true;
    }
    if (col == "name") {
        t.name = val;
        return true;
    }
    if (col == "values") {
        values = split_list(val);
        return true;
    }
    err = "unknown column '" + col + "'";
    return false;
}

// ── FK 严格校验：INSERT/UPDATE 引用存在性（列出全部缺失） ─────────────

std::string check_ench_refs(const Profile& p, const EnchInfo& e) {
    std::vector<std::string> missing;
    for (const auto& id : e.exclusive_set)
        if (!p.ench().contains(id))
            missing.push_back("exclusive_set->" + id.str());
    for (const auto& id : e.supported_items)
        if (id.is_tag() ? !p.tags().contains(id) : !p.eq().contains(id))
            missing.push_back("supported_items->" + id.str());
    std::sort(missing.begin(), missing.end());
    return join_err(missing, ", ");
}

std::string check_eq_refs(const Profile& p, const Equipment& eq) {
    if (!p.tags().contains(eq.category))
        return "category->" + eq.category.str();
    return "";
}

/// tags.values：'#' 引用须可解析（tag 已定义且解析出成员）；具体引用恒可解析。
std::string check_tag_refs(const Profile& p, const std::vector<std::string>& values) {
    const TagResolver* tr = p.tag_resolver();
    std::vector<std::string> missing;
    for (const auto& v : values) {
        if (v.empty() || v[0] != '#')
            continue;
        if (!tr || tr->resolve(v).empty())
            missing.push_back("values->" + v);
    }
    std::sort(missing.begin(), missing.end());
    return join_err(missing, ", ");
}

// ── DELETE 反向引用检查（列出来源清单） ────────────────────────────────

std::string check_delete_enchantment(const Profile& p, const NSID& id) {
    std::vector<std::string> by_exclusive;
    for (const auto& [other, info] : p.ench().data())
        if (info.exclusive_set.count(id))
            by_exclusive.push_back(other.str());
    std::sort(by_exclusive.begin(), by_exclusive.end());
    if (by_exclusive.empty())
        return "";
    return "enchantment '" + id.str() + "' is referenced by exclusive_set of " + quoted_join(by_exclusive);
}

std::string check_delete_equipment(const Profile& p, const NSID& id) {
    std::vector<std::string> parts;
    std::vector<std::string> by_supported;
    for (const auto& [eid, info] : p.ench().data())
        if (info.supported_items.count(id))
            by_supported.push_back(eid.str());
    std::sort(by_supported.begin(), by_supported.end());
    if (!by_supported.empty())
        parts.push_back("supported_items of " + quoted_join(by_supported));
    std::vector<std::string> by_values;
    if (const TagResolver* tr = p.tag_resolver()) {
        const std::string target = id.str();
        for (const auto& [tid, tag] : p.tags().data()) {
            const auto* raw = tr->raw_values(tid.get_ns() + ":" + tid.get_id());
            if (!raw)
                continue;
            for (const auto& v : *raw)
                if (const auto* e = std::get_if<EntryRef>(&v); e && e->id == target) {
                    by_values.push_back(tid.str());
                    break;
                }
        }
    }
    std::sort(by_values.begin(), by_values.end());
    if (!by_values.empty())
        parts.push_back("values of tag " + quoted_join(by_values));
    if (parts.empty())
        return "";
    return "equipment '" + id.str() + "' is referenced by " + join_err(parts, "; ");
}

std::string check_delete_tag(const Profile& p, const NSID& id) {
    std::vector<std::string> parts;
    std::vector<std::string> by_category;
    for (const auto& [eid, eq] : p.eq().data())
        if (eq.category == id)
            by_category.push_back(eid.str());
    std::sort(by_category.begin(), by_category.end());
    if (!by_category.empty())
        parts.push_back("category of " + quoted_join(by_category));
    std::vector<std::string> by_supported;
    for (const auto& [eid, info] : p.ench().data())
        if (info.supported_items.count(id))
            by_supported.push_back(eid.str());
    std::sort(by_supported.begin(), by_supported.end());
    if (!by_supported.empty())
        parts.push_back("supported_items of " + quoted_join(by_supported));
    std::vector<std::string> by_values;
    const std::string target_key = id.get_ns() + ":" + id.get_id();
    if (const TagResolver* tr = p.tag_resolver()) {
        for (const auto& [tid, tag] : p.tags().data()) {
            if (tid == id)
                continue;
            const auto* raw = tr->raw_values(tid.get_ns() + ":" + tid.get_id());
            if (!raw)
                continue;
            for (const auto& v : *raw)
                if (const auto* t = std::get_if<TagRef>(&v); t && t->key == target_key) {
                    by_values.push_back(tid.str());
                    break;
                }
        }
    }
    std::sort(by_values.begin(), by_values.end());
    if (!by_values.empty())
        parts.push_back("values of tag " + quoted_join(by_values));
    if (parts.empty())
        return "";
    return "tag '" + id.str() + "' is referenced by " + join_err(parts, "; ");
}

// ── WHERE 匹配（哨兵 col 空 = 匹配全部；常规条件比较字符串化列值） ─────

bool row_matches(const std::vector<std::string>& row, const std::vector<ColDef>& cols, const std::vector<WhereCond>& where) {
    for (const auto& w : where) {
        if (w.col.empty())
            continue; // 哨兵
        const int ci = col_index(cols, w.col);
        if (ci < 0 || row[static_cast<size_t>(ci)] != w.val)
            return false;
    }
    return true;
}

/// 收集匹配行 id（按表遍历注册表，字符串化行值比较）。
std::vector<NSID>
match_ids(const Profile& p, const std::string& table, const std::vector<ColDef>& cols, const std::vector<WhereCond>& where) {
    std::vector<NSID> out;
    if (table == "enchantment") {
        for (const auto& [id, info] : p.ench().data())
            if (row_matches(enchantment_row(info), cols, where))
                out.push_back(id);
    } else if (table == "equipment") {
        for (const auto& [id, eq] : p.eq().data())
            if (row_matches(equipment_row(eq), cols, where))
                out.push_back(id);
    } else {
        for (const auto& [id, t] : p.tags().data())
            if (row_matches(tags_row(p, t), cols, where))
                out.push_back(id);
    }
    return out;
}

// ── 语句级原子守卫：变更前深拷贝（含 TagResolver 独立副本）；未提交即恢复 ──

struct ProfileWriteGuard {
    ProfileManager& mgr;
    Profile* prof;
    Profile snapshot; // 语句前状态（可移入 UNDO 栈）
    bool committed = false;

    ProfileWriteGuard(ProfileManager& m, Profile* p) : mgr(m), prof(p), snapshot(*p) {
        if (const TagResolver* tr = p->tag_resolver())
            snapshot.set_tag_resolver(std::make_shared<TagResolver>(*tr));
    }
    ~ProfileWriteGuard() {
        if (!committed) {
            *prof = std::move(snapshot); // 恢复克隆
            mgr.notify_mutated();
        }
    }
    void commit() { committed = true; }
};

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
    if (const auto* ins = std::get_if<InsertStmt>(&stmt))
        return exec_insert(*ins);
    if (const auto* upd = std::get_if<UpdateStmt>(&stmt))
        return exec_update(*upd);
    if (const auto* del = std::get_if<DeleteStmt>(&stmt))
        return exec_delete(*del);
    // STATUS/SAVE 由 Task 4 的 SqlSession（脏跟踪 + 基线 diff + 持久化）提供。
    SqlResult r;
    r.message = "STATUS/SAVE land in Task 4";
    return r;
}

// ── 写面：INSERT ───────────────────────────────────────────────────────

SqlResult SqlExecutor::exec_insert(const InsertStmt& s) {
    SqlResult r;
    const std::vector<ColDef>* cols = table_cols(s.table);
    if (!cols) {
        r.message = "unknown table '" + s.table + "'";
        return r;
    }
    Profile* prof = _mgr.find(_current);
    if (!prof) {
        r.message = "unknown profile '" + _current + "'";
        return r;
    }

    // 列校验 + 必含 id。
    bool has_id = false;
    for (const auto& c : s.cols) {
        const int ci = col_index(*cols, c);
        if (ci < 0) {
            r.message = "unknown column '" + c + "'";
            return r;
        }
        if ((*cols)[static_cast<size_t>(ci)].name == "id")
            has_id = true;
    }
    if (!has_id) {
        r.message = "INSERT requires the 'id' column";
        return r;
    }

    if (s.table == "enchantment") {
        EnchInfo e;
        for (size_t i = 0; i < s.cols.size(); ++i)
            if (!set_field_enchantment(s.cols[i], s.vals[i], e, r.message))
                return r;
        if (prof->ench().contains(e.id)) {
            r.message = "enchantment '" + e.id.str() + "' already exists";
            return r;
        }
        const std::string fk = check_ench_refs(*prof, e);
        if (!fk.empty()) {
            r.message = "FK violation: " + fk;
            return r;
        }
        ProfileWriteGuard guard(_mgr, prof);
        prof->add_enchantment(e);
        guard.commit();
        push_undo(_current, std::move(guard.snapshot));
        _mgr.notify_mutated();
    } else if (s.table == "equipment") {
        Equipment eq;
        for (size_t i = 0; i < s.cols.size(); ++i)
            if (!set_field_equipment(s.cols[i], s.vals[i], eq, r.message))
                return r;
        if (prof->eq().contains(eq.id)) {
            r.message = "equipment '" + eq.id.str() + "' already exists";
            return r;
        }
        const std::string fk = check_eq_refs(*prof, eq);
        if (!fk.empty()) {
            r.message = "FK violation: " + fk;
            return r;
        }
        ProfileWriteGuard guard(_mgr, prof);
        prof->add_equipment(eq);
        guard.commit();
        push_undo(_current, std::move(guard.snapshot));
        _mgr.notify_mutated();
    } else { // tags
        EquipmentTag t;
        std::vector<std::string> values;
        for (size_t i = 0; i < s.cols.size(); ++i)
            if (!set_field_tags(s.cols[i], s.vals[i], t, values, r.message))
                return r;
        if (prof->tags().contains(t.id)) {
            r.message = "tag '" + t.id.str() + "' already exists";
            return r;
        }
        const std::string fk = check_tag_refs(*prof, values);
        if (!fk.empty()) {
            r.message = "FK violation: " + fk;
            return r;
        }
        ProfileWriteGuard guard(_mgr, prof);
        prof->add_tag(t);
        if (!values.empty()) {
            // values 写 = TagResolver::add_tag 替换语义（key = 去 '#' 的 id）。
            auto res = prof->tag_resolver_ptr();
            if (!res) {
                res = std::make_shared<TagResolver>();
                prof->set_tag_resolver(res);
            }
            res->add_tag(t.id.get_ns() + ":" + t.id.get_id(), std::unordered_set<std::string>(values.begin(), values.end()));
        }
        guard.commit();
        push_undo(_current, std::move(guard.snapshot));
        _mgr.notify_mutated();
    }

    r.affected = 1;
    r.message = "1 row(s) affected";
    return r;
}

// ── 写面：UPDATE ───────────────────────────────────────────────────────

SqlResult SqlExecutor::exec_update(const UpdateStmt& s) {
    SqlResult r;
    const std::vector<ColDef>* cols = table_cols(s.table);
    if (!cols) {
        r.message = "unknown table '" + s.table + "'";
        return r;
    }
    Profile* prof = _mgr.find(_current);
    if (!prof) {
        r.message = "unknown profile '" + _current + "'";
        return r;
    }

    for (const auto& [c, v] : s.sets) {
        if (col_index(*cols, c) < 0) {
            r.message = "unknown column '" + c + "'";
            return r;
        }
        if (c == "id") {
            r.message = "cannot UPDATE primary key 'id'";
            return r;
        }
    }
    for (const auto& w : s.where) {
        if (w.col.empty())
            continue; // 哨兵
        if (col_index(*cols, w.col) < 0) {
            r.message = "unknown column '" + w.col + "' in WHERE";
            return r;
        }
    }

    const std::vector<NSID> matched = match_ids(*prof, s.table, *cols, s.where);
    if (matched.empty()) {
        r.message = "0 row(s) affected";
        return r;
    }

    // 逐行构建增量并整体 FK 校验（任一行悬空 → 整句拒绝，零部分写入）。
    if (s.table == "enchantment") {
        std::vector<EnchInfo> patches;
        patches.reserve(matched.size());
        std::vector<std::string> fk_errors;
        for (const NSID& id : matched) {
            EnchInfo patch = prof->ench().at(id);
            for (const auto& [c, v] : s.sets)
                if (!set_field_enchantment(c, v, patch, r.message))
                    return r;
            const std::string fk = check_ench_refs(*prof, patch);
            if (!fk.empty())
                fk_errors.push_back("enchantment '" + id.str() + "': " + fk);
            patches.push_back(std::move(patch));
        }
        if (!fk_errors.empty()) {
            r.message = "FK violation: " + join_err(fk_errors, "; ");
            return r;
        }
        ProfileWriteGuard guard(_mgr, prof);
        for (const EnchInfo& patch : patches)
            prof->update_enchantment(patch);
        guard.commit();
        push_undo(_current, std::move(guard.snapshot));
        _mgr.notify_mutated();
    } else if (s.table == "equipment") {
        std::vector<Equipment> patches;
        patches.reserve(matched.size());
        std::vector<std::string> fk_errors;
        for (const NSID& id : matched) {
            Equipment patch = prof->eq().at(id);
            for (const auto& [c, v] : s.sets)
                if (!set_field_equipment(c, v, patch, r.message))
                    return r;
            const std::string fk = check_eq_refs(*prof, patch);
            if (!fk.empty())
                fk_errors.push_back("equipment '" + id.str() + "': " + fk);
            patches.push_back(std::move(patch));
        }
        if (!fk_errors.empty()) {
            r.message = "FK violation: " + join_err(fk_errors, "; ");
            return r;
        }
        ProfileWriteGuard guard(_mgr, prof);
        for (const Equipment& patch : patches) {
            prof->remove_equipment(patch.id);
            prof->add_equipment(patch);
        }
        guard.commit();
        push_undo(_current, std::move(guard.snapshot));
        _mgr.notify_mutated();
    } else { // tags
        std::vector<EquipmentTag> patches;
        std::vector<std::vector<std::string>> value_sets;
        std::vector<bool> touch_values;
        patches.reserve(matched.size());
        std::vector<std::string> fk_errors;
        for (const NSID& id : matched) {
            EquipmentTag patch = prof->tags().at(id);
            std::vector<std::string> values;
            for (const auto& [c, v] : s.sets)
                if (!set_field_tags(c, v, patch, values, r.message))
                    return r;
            const bool has_values =
                std::any_of(s.sets.begin(), s.sets.end(), [](const auto& kv) { return kv.first == "values"; });
            if (has_values) {
                const std::string fk = check_tag_refs(*prof, values);
                if (!fk.empty())
                    fk_errors.push_back("tag '" + id.str() + "': " + fk);
            }
            patches.push_back(std::move(patch));
            value_sets.push_back(std::move(values));
            touch_values.push_back(has_values);
        }
        if (!fk_errors.empty()) {
            r.message = "FK violation: " + join_err(fk_errors, "; ");
            return r;
        }
        ProfileWriteGuard guard(_mgr, prof);
        for (size_t i = 0; i < matched.size(); ++i) {
            prof->remove_tag(patches[i].id);
            prof->add_tag(patches[i]);
            if (touch_values[i]) {
                // REPLACE 语义：values='' 也须清空 resolver 旧值（add_tag 空集 =
                // 清空）。
                auto res = prof->tag_resolver_ptr();
                if (!res) {
                    res = std::make_shared<TagResolver>();
                    prof->set_tag_resolver(res);
                }
                res->add_tag(patches[i].id.get_ns() + ":" + patches[i].id.get_id(),
                             std::unordered_set<std::string>(value_sets[i].begin(), value_sets[i].end()));
            }
        }
        guard.commit();
        push_undo(_current, std::move(guard.snapshot));
        _mgr.notify_mutated();
    }

    r.affected = static_cast<int64_t>(matched.size());
    r.message = std::to_string(matched.size()) + " row(s) affected";
    return r;
}

// ── 写面：DELETE ───────────────────────────────────────────────────────

SqlResult SqlExecutor::exec_delete(const DeleteStmt& s) {
    SqlResult r;
    const std::vector<ColDef>* cols = table_cols(s.table);
    if (!cols) {
        r.message = "unknown table '" + s.table + "'";
        return r;
    }
    Profile* prof = _mgr.find(_current);
    if (!prof) {
        r.message = "unknown profile '" + _current + "'";
        return r;
    }
    for (const auto& w : s.where) {
        if (w.col.empty())
            continue; // 哨兵
        if (col_index(*cols, w.col) < 0) {
            r.message = "unknown column '" + w.col + "' in WHERE";
            return r;
        }
    }

    const std::vector<NSID> matched = match_ids(*prof, s.table, *cols, s.where);
    if (matched.empty()) {
        r.message = "0 row(s) affected";
        return r;
    }

    // 反向引用整体校验：任一匹配行被引用 → 整句拒绝并列出来源（零部分删除）。
    std::vector<std::string> ref_errors;
    if (s.table == "enchantment") {
        for (const NSID& id : matched) {
            const std::string e = check_delete_enchantment(*prof, id);
            if (!e.empty())
                ref_errors.push_back(e);
        }
        if (!ref_errors.empty()) {
            r.message = "cannot delete: " + join_err(ref_errors, "; ");
            return r;
        }
        ProfileWriteGuard guard(_mgr, prof);
        for (const NSID& id : matched)
            prof->remove_enchantment(id);
        guard.commit();
        push_undo(_current, std::move(guard.snapshot));
        _mgr.notify_mutated();
    } else if (s.table == "equipment") {
        for (const NSID& id : matched) {
            const std::string e = check_delete_equipment(*prof, id);
            if (!e.empty())
                ref_errors.push_back(e);
        }
        if (!ref_errors.empty()) {
            r.message = "cannot delete: " + join_err(ref_errors, "; ");
            return r;
        }
        ProfileWriteGuard guard(_mgr, prof);
        for (const NSID& id : matched)
            prof->remove_equipment(id);
        guard.commit();
        push_undo(_current, std::move(guard.snapshot));
        _mgr.notify_mutated();
    } else { // tags
        for (const NSID& id : matched) {
            const std::string e = check_delete_tag(*prof, id);
            if (!e.empty())
                ref_errors.push_back(e);
        }
        if (!ref_errors.empty()) {
            r.message = "cannot delete: " + join_err(ref_errors, "; ");
            return r;
        }
        ProfileWriteGuard guard(_mgr, prof);
        for (const NSID& id : matched)
            prof->remove_tag(id);
        guard.commit();
        push_undo(_current, std::move(guard.snapshot));
        _mgr.notify_mutated();
    }

    r.affected = static_cast<int64_t>(matched.size());
    r.message = std::to_string(matched.size()) + " row(s) affected";
    return r;
}

// ── UNDO 栈（快照 = 成功写语句前的 profile 深拷贝；容量 16，FIFO 淘汰） ──

void SqlExecutor::push_undo(const std::string& profile, Profile snapshot) {
    _undo.push_back(UndoEntry{profile, std::make_shared<Profile>(std::move(snapshot))});
    while (_undo.size() > 16)
        _undo.pop_front();
}

bool SqlExecutor::undo(std::string& err) {
    if (_undo.empty()) {
        err = "nothing to undo";
        return false;
    }
    UndoEntry entry = std::move(_undo.back());
    _undo.pop_back();
    Profile* prof = _mgr.find(entry.profile);
    if (!prof) {
        err = "profile '" + entry.profile + "' no longer exists";
        return false;
    }
    *prof = *entry.snapshot; // 恢复克隆（含 TagResolver 快照）
    _mgr.notify_mutated();
    return true;
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
