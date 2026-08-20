#pragma once
#include "domain/business/sql/SqlParser.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Profile;
class ProfileManager;

namespace business::sql {

/// 单条语句的执行结果：表头 + 行（全部字符串化）+ 影响行数 + 语句消息。
struct SqlResult {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    int64_t affected = 0;
    std::string message; // 语句消息（如 "1 row(s) affected"），可空
};

/// 片 1 SQL 执行器：查询面（SELECT/SHOW）+ 写面（INSERT/UPDATE/DELETE）。
///
/// 写面语义（片 1 严格档）：
/// - INSERT 必含 `id`，缺失列取 Profile 构造默认；列表列（exclusive_set/
///   supported_items/values）按逗号拆分逐项 NSID 化（'#' 保留）。
/// - FK 严格校验：INSERT/UPDATE 引用存在性（exclusive_set→enchantment.id、
///   supported_items #ref→tag.id / 具体→equipment.id、equipment.category→tag.id、
///   tags.values→可解析），错误列出全部缺失；DELETE 反向引用检查，错误列出来源。
/// - 语句级原子：写语句前克隆目标 profile（含 TagResolver 深拷贝），变更失败/
///   异常恢复克隆并 notify_mutated()；成功 = 提交。
/// - UNDO 栈：每次成功写语句前压入快照，容量 16（FIFO 淘汰）；undo() 恢复最近
///   一次快照。STATUS/SAVE 由 Task 4 的 SqlSession 提供。
class SqlExecutor {
public:
    SqlExecutor(ProfileManager& mgr, std::string profiles_dir);

    /// 设置当前 profile（查询/写的数据源）。不存在的名字在 execute 时报错。
    void set_current(std::string profile);

    /// 当前 profile 名（由 set_current 设置）。
    const std::string& current() const;

    /// 执行一条已解析的语句。
    SqlResult execute(const SqlStmt& stmt);

    /// 回滚最近一次成功写语句前的快照（跨 profile 全局最近优先；容量 16 FIFO）。
    /// 返回 false 且 err 非空表示无快照可回滚或目标 profile 已不存在。
    bool undo(std::string& err);

private:
    SqlResult exec_select(const SelectStmt& s);
    SqlResult exec_insert(const InsertStmt& s);
    SqlResult exec_update(const UpdateStmt& s);
    SqlResult exec_delete(const DeleteStmt& s);
    SqlResult exec_copy(const CopyStmt& s);
    SqlResult exec_merge(const MergeStmt& s);
    SqlResult exec_fork(const ForkStmt& s);

    struct UndoEntry {
        std::string profile;
        std::shared_ptr<Profile> snapshot; // created 条目为 nullptr（快照无用）
        bool created = false;              // true = 条目为 FORK 建的新 profile，undo = remove
    };
    void push_undo(const std::string& profile, Profile snapshot);
    void push_undo_created(const std::string& profile);

    ProfileManager& _mgr;
    [[maybe_unused]] std::string _profiles_dir; // 供 Task 4 SAVE 使用
    std::string _current;
    std::deque<UndoEntry> _undo;
};

} // namespace business::sql
