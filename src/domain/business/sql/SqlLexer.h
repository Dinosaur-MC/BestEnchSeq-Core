#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace business::sql {

struct SqlToken {
    enum class Kind { ident, str, int_, bool_, comma, lparen, rparen, eq, star, semi, end };
    Kind kind = Kind::end;
    std::string text; // ident/str/bool 的文本（str 已解转义；bool 为 "true"/"false"）
    int64_t ival = 0;
};

class SqlLexer {
public:
    SqlLexer() = default;
    explicit SqlLexer(std::string_view src) : _src(src) {}
    SqlToken next();
    std::string error; // 非空 = 遇非法字符/未闭合字符串

private:
    std::string_view _src;
    size_t _pos = 0;
};

} // namespace business::sql
