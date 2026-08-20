#pragma once
#include "domain/business/sql/SqlLexer.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace business::sql {

struct WhereCond {
    std::string col;
    std::string val;
    // 哨兵（WHERE true）：col.empty() && val=="true" = 匹配全部行（执行器 Task 2/3 须遵守）
};

struct SelectStmt {
    std::string table;
    std::vector<std::string> cols;
    bool star = false;
    std::vector<WhereCond> where;
    std::string order_by;
    bool desc = false;
    int64_t limit = -1, offset = 0;
};

struct InsertStmt {
    std::string table;
    std::vector<std::string> cols;
    std::vector<std::string> vals;
};
struct UpdateStmt {
    std::string table;
    std::vector<std::pair<std::string, std::string>> sets;
    std::vector<WhereCond> where;
};
struct DeleteStmt {
    std::string table;
    std::vector<WhereCond> where;
};
struct StatusStmt {
    std::string profile;
    std::string table;
};
struct SaveStmt {
    bool all = false;
};

struct UseStmt {
    std::string profile; // 不 lower（profile key 任意字符串）
};
struct CopyStmt {
    std::string table;
    std::vector<std::string> cols; // 小写
    bool star = false;
    std::string source;            // 不 lower
    std::vector<WhereCond> where;  // 可空 = 全表
    bool with_deps = false, with_ignore = false, with_override = false;
};
struct MergeStmt {
    std::string source;
    std::string dest; // 均不 lower
};
struct ForkStmt {
    std::string source;
    std::string dest; // 均不 lower
};

using SqlStmt = std::variant<SelectStmt, InsertStmt, UpdateStmt, DeleteStmt, StatusStmt, SaveStmt, UseStmt,
                             CopyStmt, MergeStmt, ForkStmt>;

class SqlParser {
public:
    std::vector<SqlStmt> parse(std::string_view statements);
    std::string error;    // 非空 = 解析失败
    size_t error_pos = 0; // 失败 token 位置（0-based token 序号）

private:
    std::vector<SqlStmt> parse_impl();
    std::vector<WhereCond> parse_where();
    void expect_ident(std::string_view expect, std::string_view stmt_name);
    void fail(std::string msg);
    SqlToken peek();
    SqlToken take();

    SqlLexer _lx;
    SqlToken _peek;
    bool _have_peek = false;
    size_t _tokens = 0;
};

} // namespace business::sql
