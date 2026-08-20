#include "domain/business/sql/SqlSession.h"
#include "common/io/json.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/types/Profile.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace business::sql {

namespace {

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

std::string join(const std::vector<std::string>& items, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i)
            out += sep;
        out += items[i];
    }
    return out;
}

/// NSID 集合 → 排序逗号拼接（与 SQL 查询面同一表示；空集 → ""）。
std::string join_sorted(const std::unordered_set<NSID>& set) {
    std::vector<std::string> items;
    items.reserve(set.size());
    for (const auto& id : set)
        items.push_back(id.str());
    std::sort(items.begin(), items.end());
    return join(items, ",");
}

/// 标签 values 列（与 SQL 查询面同表示：resolver 解析成员集，排序逗号拼接）。
std::string tag_values_str(const Profile& p, const EquipmentTag& t) {
    const TagResolver* tr = p.tag_resolver();
    if (!tr)
        return "";
    if (const auto* vals = tr->get_tag(t.id.get_ns(), t.id.get_id())) {
        std::vector<std::string> items(vals->begin(), vals->end());
        std::sort(items.begin(), items.end());
        return join(items, ",");
    }
    return "";
}

/// 文件名消毒：profile key 是任意字符串（可含 ':' 等 Windows 非法字符），
/// 而 loader 按文件内顶层 `name` 字段取 key —— 文件名不必等于 key。
/// 规则：`: \ / < > " | ? *` → `_`（Windows 非法文件名字符集）。
std::string sanitize_filename(const std::string& name) {
    std::string out = name;
    for (char& c : out)
        if (c == ':' || c == '\\' || c == '/' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            c = '_';
    return out;
}

} // namespace

SqlSession::SqlSession(ProfileManager& mgr, std::string profiles_dir)
    : _mgr(mgr), _profiles_dir(std::move(profiles_dir)), _exec(mgr, _profiles_dir) {}

// ── 会话状态 ────────────────────────────────────────────────────────────

void SqlSession::use(const std::string& profile) {
    if (!_mgr.find(profile))
        throw std::runtime_error("unknown profile '" + profile + "'");
    _exec.set_current(profile);
}

const std::string& SqlSession::current() const {
    return _exec.current();
}

// ── 语句执行（包装 executor + 脏跟踪） ─────────────────────────────────

SqlResult SqlSession::execute(const SqlStmt& stmt) {
    // STATUS/SAVE/USE 就地分发（executor 的 "land in Task 4" 分支只服务裸 executor）。
    if (const auto* st = std::get_if<StatusStmt>(&stmt)) {
        SqlResult r;
        r.message = status(*st);
        return r;
    }
    if (const auto* sv = std::get_if<SaveStmt>(&stmt)) {
        return save(sv->all);
    }
    if (const auto* us = std::get_if<UseStmt>(&stmt)) {
        // USE：会话级切换（executor 无参与；沿用 run_sql 的 use 异常兜底语义，
        // 但转语句消息不抛）。未知 → `unknown profile '<x>'`；成功 → `use: <x>`。
        // USE 不是写语句：不标脏、不取基线、不压 undo 配对（约束 10）。
        SqlResult r;
        if (!_mgr.find(us->profile)) {
            r.message = "unknown profile '" + us->profile + "'";
            return r;
        }
        _exec.set_current(us->profile);
        r.message = "use: " + us->profile;
        return r;
    }

    // 写目标：INSERT/UPDATE/DELETE/COPY → 当前 profile；MERGE/FORK → stmt.dest
    // （FORK 不切换 current——executor 内写目标即 dest，勿依赖 _current）。
    const std::string* target = &_exec.current();
    if (const auto* mg = std::get_if<MergeStmt>(&stmt))
        target = &mg->dest;
    else if (const auto* fk = std::get_if<ForkStmt>(&stmt))
        target = &fk->dest;

    const bool is_write = std::holds_alternative<InsertStmt>(stmt) || std::holds_alternative<UpdateStmt>(stmt) ||
                          std::holds_alternative<DeleteStmt>(stmt) || std::holds_alternative<CopyStmt>(stmt) ||
                          std::holds_alternative<MergeStmt>(stmt) || std::holds_alternative<ForkStmt>(stmt);
    if (is_write && !_dirty.count(*target) && !_baselines.count(*target)) {
        // 首次写前取基线快照（= 会话起点状态，因为此前无成功写）。
        if (const Profile* p = _mgr.find(*target))
            _baselines[*target] = clone_with_resolver(*p);
        else if (std::holds_alternative<ForkStmt>(stmt))
            // FORK 目标语句前不存在（executor 预检保证 dest 不存在）→ 基线 = 空
            // Profile：STATUS diff 显示派生内容全 +（约束 10）。
            _baselines[*target] = Profile(*target);
    }

    SqlResult r = _exec.execute(stmt);
    if (is_write && r.affected > 0) {
        mark_dirty(*target);
        _write_history.push_back(*target);
        // 与 executor 的 UNDO 栈容量（16，FIFO）对齐：旧写序不再可回滚。
        while (_write_history.size() > 16)
            _write_history.erase(_write_history.begin());
    }
    return r;
}

bool SqlSession::undo(std::string& err) {
    const bool ok = _exec.undo(err);
    // F5（终审，defer 文档化）：executor undo 失败（配对 profile 被外部删除——
    // SQL 面不可达，如 FORK 出的 profile 被外部移除后其 created 条目 remove 失败）
    // 时 ok=false → _write_history 不弹栈 → 配对条目保留。下一次成功的 undo 会弹出
    // 该幽灵名，与"实际被恢复的 profile"错配（标脏/清脏落错对象）。已知边界，
    // 不做行为变更（外部删除非 SQL 可达路径）。
    if (ok && !_write_history.empty()) {
        // executor 的 UNDO 栈与 session 的写序同步（都只在成功写时入栈），
        // 弹栈目标 = 最近一次成功写的 profile。
        const std::string prof = _write_history.back();
        _write_history.pop_back();
        // 配对清理（约束 9/10）：UNDO 后配对 profile 已不存在 → 清 _dirty/_baselines
        // 中该名（两情形：FORK 的 created 条目已 remove 新 profile；或 ghost-dirty
        // ——写过但被外部删除的 profile，undo 不得复标一个已不存在的名）；
        // 否则按现状标脏（spec：UNDO 后标脏，即使无实际差）。
        if (_mgr.find(prof) == nullptr) {
            _dirty.erase(prof);
            _baselines.erase(prof);
        } else {
            mark_dirty(prof);
        }
    }
    return ok;
}

// ── 持久化 ──────────────────────────────────────────────────────────────

SqlResult SqlSession::save(bool all) {
    SqlResult r;
    std::vector<std::string> targets;
    if (all) {
        targets = dirty_profiles();
    } else if (_dirty.count(_exec.current())) {
        targets.push_back(_exec.current());
    }
    if (targets.empty()) {
        r.message = "nothing to save";
        return r;
    }

    std::vector<std::string> saved;
    std::vector<std::string> warnings;
    for (const std::string& name : targets) {
        const Profile* p = _mgr.find(name);
        if (!p) {
            // 脏 profile 已被外部移除（SQL 面不可达：manager 直删）→ 无法保存，
            // 清脏 + 清基线后跳过（D6：全部目标外删 → saved 空 → "nothing to save"；
            // 基线对已不存在的 profile 无意义，一并清理）。
            _dirty.erase(name);
            _baselines.erase(name);
            continue;
        }
        // C2（终审）：文件名消毒碰撞守卫——两个不同 key 消毒后映射到同一文件
        // （如 'a:b' 与 'a_b' → a_b.json），后写者会静默覆盖前者的持久化内容
        // （load_directory 按文件内 name 回读，后写者胜 → 前一个 profile 内容
        // 丢失）。会话内认领表记录每个已写文件名 → key：同 key 重写放行；
        // 不同 key 撞同一文件名 → 本语句失败并指名两个 key。
        const std::string fname = sanitize_filename(name) + ".json";
        const auto claim = _claimed.find(fname);
        if (claim != _claimed.end() && claim->second != name) {
            r.message = "save collision: '" + claim->second + "' and '" + name + "' both map to '" + fname + "'";
            return r;
        }
        const std::filesystem::path path = std::filesystem::path(_profiles_dir) / fname;
        if (!write_profile_file(path, *p)) {
            r.message = "save failed: " + path.string(); // 保持脏
            return r;
        }
        _claimed[fname] = name; // 本会话认领该文件名（同 key 重写仅刷新认领）
        // I5（终审）：datapack 来源 profile 的 native SAVE 与 reload 时其
        // datapack 目录构成双来源（同名文件 + 同名目录 → 回读胜者不确定），
        // 发警告不拦阻。
        if (_mgr.is_datapack_sourced(name))
            warnings.push_back("warning: '" + name +
                               "' is datapack-sourced; native save may collide with its datapack dir on reload");
        _dirty.erase(name);
        // F5（片 1 T4 minor）：基线 = 脏 profile 专用——SAVE 后非脏 profile 不再
        // 保留基线（下次写语句会经 execute() 首写分支重新取会话起点快照）。
        // 删除而非重置：STATUS 对非脏 profile 走 base=cur（无基线 → 无差），与
        // 旧"重置基线为当前状态"观察等价，且不残留陈旧克隆。
        _baselines.erase(name);
        saved.push_back(name);
    }
    r.message = saved.empty() ? std::string("nothing to save") : "saved: " + join(saved, ", ");
    if (!warnings.empty())
        r.message += "\n" + join(warnings, "\n");
    return r;
}

// ── 状态 ────────────────────────────────────────────────────────────────

std::vector<std::string> SqlSession::dirty_profiles() const {
    std::vector<std::string> out(_dirty.begin(), _dirty.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::string SqlSession::status(const StatusStmt& s) const {
    const std::string prof = s.profile.empty() ? _exec.current() : s.profile;
    const Profile* cur = _mgr.find(prof);
    if (!cur)
        return "unknown profile '" + prof + "'";

    // 基线 = 上次 SAVE 的克隆（从未 SAVE/未脏 → 无基线 → 视同当前，零差）。
    const Profile* base = cur;
    const auto it = _baselines.find(prof);
    if (it != _baselines.end())
        base = &it->second;

    std::string out = "profile: " + prof;
    if (_dirty.count(prof))
        out += " (dirty)";

    static const std::array<std::string_view, 3> all_tables = {"enchantment", "equipment", "tags"};
    const std::vector<std::string_view> tables = s.table.empty()
                                                     ? std::vector<std::string_view>(all_tables.begin(), all_tables.end())
                                                     : std::vector<std::string_view>{s.table};
    for (const auto& t : tables)
        out += "\n  " + std::string(t) + ": " + diff_table(std::string(t), *base, *cur);
    return out;
}

std::string SqlSession::unsaved_warning() const {
    if (_dirty.empty())
        return "";
    return "unsaved changes in: " + join(dirty_profiles(), ", ") + " \u2014 run SAVE to persist";
}

// ── 私有：克隆 / SAVE 组合 / 文件写出 / diff ────────────────────────────

Profile SqlSession::clone_with_resolver(const Profile& p) {
    Profile c = p; // 注册表值拷贝；resolver 是 shared_ptr → 需显式深拷贝
    if (const TagResolver* tr = p.tag_resolver())
        c.set_tag_resolver(std::make_shared<TagResolver>(*tr));
    return c;
}

void SqlSession::mark_dirty(const std::string& profile) {
    _dirty.insert(profile);
}

Json SqlSession::compose_json(const Profile& p) const {
    Json obj = p.to_json();
    // tags：数组 → 对象（loader 原生格式 "key": [values]，与 vanilla.json 一致）。
    // values 取 resolver 的 raw_values 原始条目（EntryRef 原样；TagRef 补 '#'），
    // 使 tag 成员关系（含 '#ref'）随文件持久化。
    Json tags = Json::object();
    for (const auto& [id, tag] : p.tags().data()) {
        const std::string key = id.get_ns() + ":" + id.get_id();
        Json::Array vals;
        if (const TagResolver* tr = p.tag_resolver()) {
            if (const auto* raw = tr->raw_values(key)) {
                for (const auto& v : *raw) {
                    if (const auto* e = std::get_if<EntryRef>(&v))
                        vals.push_back(Json(e->id));
                    else if (const auto* t = std::get_if<TagRef>(&v))
                        vals.push_back(Json("#" + t->key));
                }
            }
        }
        tags.set(key, Json(std::move(vals)));
    }
    obj.set(std::string(ProfileMetadata::KEY_TAGS), std::move(tags));
    return obj;
}

bool SqlSession::write_profile_file(const std::filesystem::path& path, const Profile& p) const {
    try {
        const Json obj = compose_json(p);
        std::ofstream out(path);
        if (!out)
            return false;
        out << obj.to_string(Json::Pretty);
        out.flush();
        return out.good();
    } catch (const std::exception&) {
        return false;
    } catch (...) {
        return false;
    }
}

std::string SqlSession::diff_table(const std::string& table, const Profile& base, const Profile& cur) const {
    const std::map<std::string, Row> b = table_rows(base, table);
    const std::map<std::string, Row> c = table_rows(cur, table);

    // 行 id 并集（排序）；比较字符串化列值（列表列 = 排序拼接 → 排序后比较）。
    std::vector<std::string> ids;
    ids.reserve(b.size() + c.size());
    for (const auto& [id, row] : b)
        ids.push_back(id);
    for (const auto& [id, row] : c)
        if (!b.count(id))
            ids.push_back(id);
    std::sort(ids.begin(), ids.end());

    std::vector<std::string> entries;
    for (const auto& id : ids) {
        const bool in_b = b.count(id) != 0;
        const bool in_c = c.count(id) != 0;
        if (in_c && !in_b) {
            entries.push_back("+" + id);
        } else if (in_b && !in_c) {
            entries.push_back("-" + id);
        } else {
            const Row& bf = b.at(id);
            const Row& cf = c.at(id);
            // 显式尺寸守卫（片 1 T4 minor）：当前 table_rows 恒产出完整列集
            // （行非空），min 实际无空向量风险；但两侧行若缺列（未来列集演进），
            // 空 Row 直接跳过 diff——比在 std::min 上隐式靠 n==0 短路更明确。
            if (bf.empty() || cf.empty())
                continue;
            std::vector<std::string> changes;
            const size_t n = std::min(bf.size(), cf.size());
            for (size_t i = 0; i < n; ++i)
                if (bf[i].second != cf[i].second)
                    changes.push_back(bf[i].first + ": " + bf[i].second + "->" + cf[i].second);
            if (!changes.empty())
                entries.push_back("~" + id + "(" + join(changes, ", ") + ")");
        }
    }
    if (entries.empty())
        return "(no changes)";
    return join(entries, "  ");
}

std::map<std::string, SqlSession::Row> SqlSession::table_rows(const Profile& p, const std::string& table) const {
    std::map<std::string, Row> out;
    if (table == "enchantment") {
        for (const auto& [id, e] : p.ench().data()) {
            out[id.str()] = {
                {"name", e.name},
                {"supported_platform", std::string(mce_str(e.supported_platform))},
                {"max_level", std::to_string(e.max_level)},
                {"limited_level", std::to_string(e.limited_level)},
                {"multiplier", std::to_string(e.multiplier)},
                {"is_treasure", e.is_treasure ? "true" : "false"},
                {"exclusive_set", join_sorted(e.exclusive_set)},
                {"supported_items", join_sorted(e.supported_items)},
                {"min_cost_base", std::to_string(e.min_cost_base)},
                {"min_cost_per_level", std::to_string(e.min_cost_per_level)},
            };
        }
    } else if (table == "equipment") {
        for (const auto& [id, eq] : p.eq().data()) {
            out[id.str()] = {
                {"name", eq.name},
                {"category", eq.category.str()},
                {"max_durability", std::to_string(eq.max_durability)},
            };
        }
    } else { // tags
        for (const auto& [id, t] : p.tags().data()) {
            out[id.str()] = {
                {"name", t.name},
                {"values", tag_values_str(p, t)},
            };
        }
    }
    return out;
}

} // namespace business::sql
