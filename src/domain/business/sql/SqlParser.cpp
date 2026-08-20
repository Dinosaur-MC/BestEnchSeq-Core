#include "domain/business/sql/SqlParser.h"
#include <cctype>
#include <unordered_set>

namespace business::sql {

namespace {
const std::unordered_set<std::string>& tables() {
    static const std::unordered_set<std::string> t{"enchantment", "equipment", "tags"};
    return t;
}
std::string lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

SqlToken SqlParser::peek() {
    if (!_have_peek) {
        _peek = _lx.next();
        _have_peek = true;
    }
    return _peek;
}
SqlToken SqlParser::take() {
    auto t = peek();
    _have_peek = false;
    ++_tokens;
    return t;
}

void SqlParser::fail(std::string msg) {
    if (error.empty()) {
        error = std::move(msg);
        error_pos = _tokens;
    }
}

std::vector<SqlStmt> SqlParser::parse(std::string_view statements) {
    error.clear();
    error_pos = 0;
    _tokens = 0;
    _have_peek = false;
    _lx = SqlLexer(statements);
    return parse_impl();
}

std::vector<SqlStmt> SqlParser::parse_impl() {
    std::vector<SqlStmt> out;
    while (true) {
        SqlToken t = peek();
        if (t.kind == SqlToken::Kind::end)
            break;
        if (t.kind == SqlToken::Kind::semi) {
            take();
            continue;
        }
        if (t.kind != SqlToken::Kind::ident) {
            fail("expected statement");
            break;
        }
        const std::string kw = lower(t.text);
        take();
        if (kw == "select" || kw == "show") {
            SelectStmt s;
            if (kw == "show")
                s.star = true;
            else if (peek().kind == SqlToken::Kind::star) {
                take();
                s.star = true;
            } else {
                while (true) {
                    if (peek().kind != SqlToken::Kind::ident) {
                        fail("expected column");
                        break;
                    }
                    // 列名大小写不敏感（spec §2.2）：解析期归一为小写，执行器按
                    // 小写列元数据匹配。
                    s.cols.push_back(lower(take().text));
                    if (peek().kind == SqlToken::Kind::comma) {
                        take();
                        continue;
                    }
                    break;
                }
            }
            if (lower(peek().text) != "from") {
                fail("expected FROM");
                break;
            }
            take();
            s.table = lower(take().text);
            if (!tables().count(s.table)) {
                fail("unknown table '" + s.table + "'");
                break;
            }
            if (lower(peek().text) == "where") {
                take();
                s.where = parse_where();
            }
            if (lower(peek().text) == "order") {
                take();
                if (lower(peek().text) != "by") {
                    fail("expected BY");
                    break;
                }
                take();
                s.order_by = lower(take().text);
                if (lower(peek().text) == "desc") {
                    take();
                    s.desc = true;
                }
            }
            if (lower(peek().text) == "limit") {
                take();
                if (peek().kind != SqlToken::Kind::int_) {
                    fail("expected integer after LIMIT");
                    break;
                }
                s.limit = take().ival;
            }
            if (lower(peek().text) == "offset") {
                take();
                if (peek().kind != SqlToken::Kind::int_) {
                    fail("expected integer after OFFSET");
                    break;
                }
                s.offset = take().ival;
            }
            out.push_back(std::move(s));
        } else if (kw == "insert") {
            if (lower(peek().text) != "into") {
                fail("expected INTO");
                break;
            }
            take();
            InsertStmt s;
            s.table = lower(take().text);
            if (!tables().count(s.table)) {
                fail("unknown table '" + s.table + "'");
                break;
            }
            if (peek().kind != SqlToken::Kind::lparen) {
                fail("expected ( columns )");
                break;
            }
            take();
            while (true) {
                if (peek().kind != SqlToken::Kind::ident) {
                    fail("expected column name");
                    break;
                }
                s.cols.push_back(lower(take().text));
                if (peek().kind == SqlToken::Kind::comma) {
                    take();
                    continue;
                }
                break;
            }
            if (peek().kind != SqlToken::Kind::rparen) {
                fail("expected )");
                break;
            }
            take();
            if (lower(peek().text) != "values") {
                fail("expected VALUES");
                break;
            }
            take();
            if (peek().kind != SqlToken::Kind::lparen) {
                fail("expected ( values )");
                break;
            }
            take();
            while (true) {
                SqlToken v = peek();
                if (v.kind == SqlToken::Kind::str || v.kind == SqlToken::Kind::int_ || v.kind == SqlToken::Kind::bool_ ||
                    v.kind == SqlToken::Kind::ident) {
                    s.vals.push_back(take().text);
                } else {
                    fail("expected value");
                    break;
                }
                if (peek().kind == SqlToken::Kind::comma) {
                    take();
                    continue;
                }
                break;
            }
            if (peek().kind != SqlToken::Kind::rparen) {
                fail("expected )");
                break;
            }
            take();
            if (s.cols.size() != s.vals.size()) {
                fail("column/value count mismatch");
                break;
            }
            out.push_back(std::move(s));
        } else if (kw == "update") {
            UpdateStmt s;
            s.table = lower(take().text);
            if (!tables().count(s.table)) {
                fail("unknown table '" + s.table + "'");
                break;
            }
            if (lower(peek().text) != "set") {
                fail("expected SET");
                break;
            }
            take();
            while (true) {
                if (peek().kind != SqlToken::Kind::ident) {
                    fail("expected column");
                    break;
                }
                std::string col = lower(take().text);
                if (peek().kind != SqlToken::Kind::eq) {
                    fail("expected =");
                    break;
                }
                take();
                SqlToken v = peek();
                if (v.kind != SqlToken::Kind::str && v.kind != SqlToken::Kind::int_ && v.kind != SqlToken::Kind::bool_ &&
                    v.kind != SqlToken::Kind::ident) {
                    fail("expected value");
                    break;
                }
                s.sets.emplace_back(std::move(col), take().text);
                if (peek().kind == SqlToken::Kind::comma) {
                    take();
                    continue;
                }
                break;
            }
            if (lower(peek().text) == "where") {
                take();
                s.where = parse_where();
            }
            if (s.where.empty()) {
                fail("UPDATE requires WHERE (use WHERE true for all rows)");
                break;
            }
            out.push_back(std::move(s));
        } else if (kw == "delete") {
            if (lower(peek().text) != "from") {
                fail("expected FROM");
                break;
            }
            take();
            DeleteStmt s;
            s.table = lower(take().text);
            if (!tables().count(s.table)) {
                fail("unknown table '" + s.table + "'");
                break;
            }
            if (lower(peek().text) == "where") {
                take();
                s.where = parse_where();
            }
            if (s.where.empty()) {
                fail("DELETE requires WHERE (use WHERE true for all rows)");
                break;
            }
            out.push_back(std::move(s));
        } else if (kw == "use") {
            UseStmt s;
            if (peek().kind != SqlToken::Kind::ident) {
                fail("expected profile name after USE");
                break;
            }
            s.profile = take().text; // profile key：不 lower
            out.push_back(std::move(s));
        } else if (kw == "copy") {
            CopyStmt s;
            if (peek().kind == SqlToken::Kind::star) {
                take();
                s.star = true;
            } else {
                while (true) {
                    if (peek().kind != SqlToken::Kind::ident) {
                        fail("expected column");
                        break;
                    }
                    s.cols.push_back(lower(take().text));
                    if (peek().kind == SqlToken::Kind::comma) {
                        take();
                        continue;
                    }
                    break;
                }
                if (s.cols.empty()) {
                    fail("expected columns or * in COPY");
                    break;
                }
            }
            if (lower(peek().text) != "from") {
                fail("expected FROM");
                break;
            }
            take();
            if (peek().kind != SqlToken::Kind::ident) {
                fail("expected source profile after FROM");
                break;
            }
            s.source = take().text; // 不 lower
            if (lower(peek().text) != "into") {
                fail("expected INTO");
                break;
            }
            take();
            s.table = lower(take().text);
            if (!tables().count(s.table)) {
                fail("unknown table '" + s.table + "'");
                break;
            }
            if (lower(peek().text) == "where") {
                take();
                s.where = parse_where();
            }
            if (lower(peek().text) == "with") {
                take();
                while (true) {
                    if (peek().kind != SqlToken::Kind::ident) {
                        fail("expected WITH modifier (OVERRIDE|DEPS|IGNORE)");
                        break;
                    }
                    const std::string mod = lower(take().text);
                    if (mod == "deps") {
                        if (s.with_deps) {
                            fail("duplicate WITH DEPS");
                            break;
                        }
                        s.with_deps = true;
                    } else if (mod == "ignore") {
                        if (s.with_ignore) {
                            fail("duplicate WITH IGNORE");
                            break;
                        }
                        s.with_ignore = true;
                    } else if (mod == "override") {
                        if (s.with_override) {
                            fail("duplicate WITH OVERRIDE");
                            break;
                        }
                        s.with_override = true;
                    } else {
                        fail("unknown WITH modifier '" + mod + "'");
                        break;
                    }
                    if (peek().kind == SqlToken::Kind::comma) {
                        take();
                        continue;
                    }
                    break;
                }
                if (s.with_deps && s.with_ignore) {
                    fail("WITH DEPS and WITH IGNORE are mutually exclusive");
                    break;
                }
            }
            // WITH 修饰符错误（unknown/duplicate）只 break 内层循环，须在此上浮：
            // 与 slice 1 行为一致——任何解析错误 → 零语句返回（否则会携带半解析
            // CopyStmt，破坏 run_sql 零执行保证）。
            if (!error.empty())
                break;
            out.push_back(std::move(s));
        } else if (kw == "merge") {
            if (lower(peek().text) != "into") {
                fail("expected INTO");
                break;
            }
            take();
            MergeStmt s;
            if (peek().kind != SqlToken::Kind::ident) {
                fail("expected destination profile after INTO");
                break;
            }
            s.dest = take().text;
            if (lower(peek().text) != "from") {
                fail("expected FROM");
                break;
            }
            take();
            if (peek().kind != SqlToken::Kind::ident) {
                fail("expected source profile after FROM");
                break;
            }
            s.source = take().text;
            out.push_back(std::move(s));
        } else if (kw == "fork") {
            ForkStmt s;
            if (peek().kind != SqlToken::Kind::ident) {
                fail("expected source profile after FORK");
                break;
            }
            s.source = take().text;
            if (lower(peek().text) != "as") {
                fail("expected AS");
                break;
            }
            take();
            if (peek().kind != SqlToken::Kind::ident) {
                fail("expected new profile name after AS");
                break;
            }
            s.dest = take().text;
            out.push_back(std::move(s));
        } else if (kw == "status") {
            StatusStmt s;
            if (peek().kind == SqlToken::Kind::ident) {
                std::string a = take().text;
                if (lower(a) == "where") {
                    fail("STATUS takes [profile] [table], not WHERE");
                    break;
                }
                s.profile = a;
                if (peek().kind == SqlToken::Kind::ident)
                    s.table = lower(take().text);
            }
            out.push_back(std::move(s));
        } else if (kw == "save") {
            SaveStmt s;
            if (lower(peek().text) == "all") {
                take();
                s.all = true;
            }
            out.push_back(std::move(s));
        } else {
            fail("unsupported statement '" + kw +
                 "' (slice 2 supports SELECT/SHOW/INSERT/UPDATE/DELETE/STATUS/SAVE/USE/COPY/MERGE/FORK)");
            break;
        }
        if (!error.empty())
            break;
        if (peek().kind != SqlToken::Kind::semi && peek().kind != SqlToken::Kind::end) {
            fail("expected ';'");
            break;
        }
        if (peek().kind == SqlToken::Kind::semi)
            take();
    }
    // lexer 错误（未闭合字符串/非法字符 → next() 置 error 并返回 end token）必须
    // 上浮为解析错误：否则 parse 会"成功"地丢弃畸形尾部，run_sql 零执行保证被攻破。
    // 已解析出的语句一并清空（整体输入畸形 → 返回零语句，与零执行语义一致）。
    if (!_lx.error.empty() && error.empty()) {
        fail("lexer error: " + _lx.error);
        out.clear();
    }
    return out;
}

std::vector<WhereCond> SqlParser::parse_where() {
    std::vector<WhereCond> out;
    SqlToken first = peek();
    if (first.kind == SqlToken::Kind::bool_ && first.text == "true") {
        // 哨兵：WHERE true = 匹配全部行；单条件，不允许再跟 AND
        take();
        out.push_back(WhereCond{"", "true"});
        if (lower(peek().text) == "and") {
            fail("WHERE true matches all rows; cannot combine with AND");
        }
        return out;
    }
    while (true) {
        if (peek().kind != SqlToken::Kind::ident) {
            fail("expected condition column");
            break;
        }
        WhereCond c;
        c.col = lower(take().text);
        if (peek().kind != SqlToken::Kind::eq) {
            fail("expected = in condition");
            break;
        }
        take();
        SqlToken v = peek();
        if (v.kind == SqlToken::Kind::str || v.kind == SqlToken::Kind::int_ || v.kind == SqlToken::Kind::bool_ ||
            v.kind == SqlToken::Kind::ident)
            c.val = take().text;
        else {
            fail("expected condition value");
            break;
        }
        out.push_back(std::move(c));
        if (lower(peek().text) == "and") {
            take();
            continue;
        }
        break;
    }
    return out;
}

} // namespace business::sql
