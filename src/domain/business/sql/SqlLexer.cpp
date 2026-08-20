#include "domain/business/sql/SqlLexer.h"

#include <cctype>
#include <stdexcept>

namespace business::sql {

namespace {
bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9') || c == '.' || c == ':';
}
} // namespace

SqlToken SqlLexer::next() {
    // 跳过空白与 `--` 行注释
    while (_pos < _src.size()) {
        char c = _src[_pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++_pos;
            continue;
        }
        if (c == '-' && _pos + 1 < _src.size() && _src[_pos + 1] == '-') {
            while (_pos < _src.size() && _src[_pos] != '\n')
                ++_pos;
            continue;
        }
        break;
    }
    if (_pos >= _src.size())
        return {};
    const char c = _src[_pos];
    if (c == ',') {
        ++_pos;
        return {SqlToken::Kind::comma, ",", 0};
    }
    if (c == '(') {
        ++_pos;
        return {SqlToken::Kind::lparen, "(", 0};
    }
    if (c == ')') {
        ++_pos;
        return {SqlToken::Kind::rparen, ")", 0};
    }
    if (c == '=') {
        ++_pos;
        return {SqlToken::Kind::eq, "=", 0};
    }
    if (c == '*') {
        ++_pos;
        return {SqlToken::Kind::star, "*", 0};
    }
    if (c == ';') {
        ++_pos;
        return {SqlToken::Kind::semi, ";", 0};
    }
    if (c == '\'') {
        ++_pos;
        std::string s;
        while (_pos < _src.size()) {
            char d = _src[_pos];
            if (d == '\'') {
                if (_pos + 1 < _src.size() && _src[_pos + 1] == '\'') {
                    s += '\'';
                    _pos += 2;
                    continue;
                }
                ++_pos;
                return {SqlToken::Kind::str, std::move(s), 0};
            }
            s += d;
            ++_pos;
        }
        error = "unterminated string literal";
        return {};
    }
    if (c >= '0' && c <= '9') {
        size_t start = _pos;
        while (_pos < _src.size() && _src[_pos] >= '0' && _src[_pos] <= '9')
            ++_pos;
        const std::string digits(_src.substr(start, _pos - start));
        try {
            return {SqlToken::Kind::int_, digits, std::stoll(digits)};
        } catch (const std::out_of_range&) {
            // 超长数字字面量（int64 溢出）：std::stoll 抛 out_of_range——捕获并
            // 转 lexer 错误（零执行路径，与未闭合字符串同一模式）。不得让异常
            // 逃出 parse：run_sql 的 parse 在 try 外，逃逸会以裸 stoll 消息打穿
            // CLI 输出（exit 1 "stoll argument out of range"）并杀死 REPL。
            error = "integer literal out of range";
            return {};
        }
    }
    if (is_ident_start(c)) {
        size_t start = _pos;
        while (_pos < _src.size() && is_ident_char(_src[_pos]))
            ++_pos;
        std::string w(_src.substr(start, _pos - start));
        // TRUE/FALSE 大小写不敏感（spec §2.2）：bool 关键字按小写文本比较，
        // 并归一为小写 token 文本（执行器 parse_bool/哨兵只认 "true"/"false"）。
        std::string lw = w;
        for (char& ch : lw)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lw == "true" || lw == "false")
            return {SqlToken::Kind::bool_, std::move(lw), 0};
        return {SqlToken::Kind::ident, std::move(w), 0};
    }
    error = std::string("unexpected character '") + c + "'";
    return {};
}

} // namespace business::sql
