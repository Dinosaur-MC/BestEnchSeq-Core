#pragma once
#include "common/io/json.h"
#include "domain/business/sql/SqlExecutor.h"
#include "domain/business/sql/SqlParser.h"

#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class Profile;
class ProfileManager;

namespace business::sql {

/// 片 1 SQL 会话：包装 SqlExecutor + 会话状态（脏跟踪 / SAVE 基线 / STATUS diff）。
///
/// 职责边界（与 SqlExecutor 的分工）：
/// - executor = 语句语义（查询/写面/FK/原子/UNDO 栈）；
/// - session   = 脏集合、SAVE 基线（每脏 profile 一份含 TagResolver 的深拷贝
///   克隆）、SAVE 持久化（to_json + tags values 组合）、STATUS 基线 diff。
///
/// 脏跟踪：写语句成功 → 目标 profile 标脏（首次标脏前先取基线快照）；
/// UNDO 成功 → 标脏（spec：UNDO 后标脏，即使无实际差）；SAVE 成功 → 清脏 +
/// 重置基线为当前状态。基线 = 上次 SAVE 时的克隆（从未 SAVE 则 = 会话起点，
/// 由首次写前的快照充当）。
///
/// SAVE 组合：`Profile::to_json()` 输出 + 把 `tags` 数组替换为对象（loader
/// 原生格式 `"<ns>:<path>": [values...]`，与 vanilla.json 一致；values 取
/// TagResolver::raw_values 原始条目，'#' 引用原样保留）。没有该组合，tag
/// 成员关系只活在运行时 resolver，SAVE 后即丢失（Task 3 review carry-forward）。
class SqlSession {
public:
    SqlSession(ProfileManager& mgr, std::string profiles_dir);

    // ── 会话状态 ────────────────────────────────────────────────────

    /// 设置工作 profile（不存在 → std::runtime_error，void 无错误通道）。
    void use(const std::string& profile);

    /// 当前工作 profile 名（由 use 设置；未设置 = ""）。
    const std::string& current() const;

    // ── 语句执行（包装 executor + 脏跟踪） ───────────────────────────

    /// 读语句透传 executor；写语句成功 → 标脏；STATUS/SAVE 语句就地分发
    /// （结果经 SqlResult::message 返回）。
    SqlResult execute(const SqlStmt& stmt);

    /// 委托 executor UNDO 栈；成功后标脏（恢复的 profile = 最近成功写语句
    /// 的目标，经 _write_history 配对）。
    bool undo(std::string& err);

    // ── 持久化 ───────────────────────────────────────────────────────

    /// SAVE：all=false 只存当前 profile（脏时）；all=true 存全部脏 profile。
    /// 写 `<profiles_dir>/<name>.json`（to_json + tags values 组合）；
    /// 失败 → 错误消息 + 保持脏。成功 → `saved: a, b`；无脏 → `nothing to save`。
    SqlResult save(bool all);

    // ── 状态 ─────────────────────────────────────────────────────────

    /// 脏 profile 名（排序）。
    std::vector<std::string> dirty_profiles() const;

    /// STATUS diff 摘要（对基线；三表，`+`/`~`(字段: 旧->新)/`-`，行按 id；
    /// profile 缺省 = 当前，table 缺省 = 三表；无差 → `(no changes)`）：
    ///   profile: <name> (dirty)
    ///     enchantment: +id1  ~id2(field: old->new)  -id3
    ///     equipment: (no changes)
    ///     tags: +id4
    std::string status(const StatusStmt& s) const;

    /// 退出警告："unsaved changes in: a, b — run SAVE to persist"；无脏 → ""。
    std::string unsaved_warning() const;

private:
    /// 深拷贝 profile + TagResolver（基线/快照与活动 profile 隔离）。
    static Profile clone_with_resolver(const Profile& p);

    void mark_dirty(const std::string& profile);

    /// SAVE 组合 JSON：to_json() + tags 对象（key → 原始 values）。
    Json compose_json(const Profile& p) const;

    bool write_profile_file(const std::filesystem::path& path, const Profile& p) const;

    /// 单表基线 diff（id → 行；行 = 列序 (字段, 字符串化值) 列表）。
    using Row = std::vector<std::pair<std::string, std::string>>;
    std::string diff_table(const std::string& table, const Profile& base, const Profile& cur) const;
    std::map<std::string, Row> table_rows(const Profile& p, const std::string& table) const;

    ProfileManager& _mgr;
    std::string _profiles_dir;
    SqlExecutor _exec;
    std::unordered_set<std::string> _dirty;
    std::unordered_map<std::string, Profile> _baselines;   // 每脏 profile 一份克隆（SAVE 时重置）
    std::vector<std::string> _write_history;               // 成功写语句的 profile 序（UNDO 配对）
    std::unordered_map<std::string, std::string> _claimed; // sanitized 文件名 → 认领 key（C2 碰撞守卫，会话生命周期）
};

} // namespace business::sql
