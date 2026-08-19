#pragma once
#include "domain/business/sql/SqlParser.h"
#include <cstdint>
#include <string>
#include <vector>

class ProfileManager;

namespace business::sql {

/// 单条语句的执行结果：表头 + 行（全部字符串化）+ 影响行数 + 语句消息。
struct SqlResult {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    int64_t affected = 0;
    std::string message; // 语句消息（如 "1 row(s) affected"），可空
};

/// 片 1 查询面执行器：SELECT/SHOW（WHERE/ORDER BY/LIMIT/OFFSET + 列投影）。
/// 写面（INSERT/UPDATE/DELETE/STATUS/SAVE）在 Task 3 实现——本执行器对写语句
/// 返回错误结果（message 注明 "land in Task 3"），不产生任何副作用。
class SqlExecutor {
public:
    SqlExecutor(ProfileManager& mgr, std::string profiles_dir);

    /// 设置当前 profile（查询的数据源）。不存在的名字在 execute 时报错。
    void set_current(std::string profile);

    /// 当前 profile 名（由 set_current 设置）。
    const std::string& current() const;

    /// 执行一条已解析的语句。
    SqlResult execute(const SqlStmt& stmt);

private:
    SqlResult exec_select(const SelectStmt& s);

    ProfileManager& _mgr;
    [[maybe_unused]] std::string _profiles_dir; // 供 Task 3 写面（SAVE）使用
    std::string _current;
};

} // namespace business::sql
