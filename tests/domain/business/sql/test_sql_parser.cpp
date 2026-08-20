#define BESQ_TEST_MAIN
#include "domain/business/sql/SqlParser.h"
#include "framework/test_framework.h"
using namespace business::sql;

TEST_CASE("sql_lexer_basics") {
    SqlLexer lx("SELECT id, name FROM enchantment WHERE max_level=5;");
    expect(lx.next().kind == SqlToken::Kind::ident, "SELECT");
    expect(lx.next().kind == SqlToken::Kind::ident, "id");
    expect(lx.next().kind == SqlToken::Kind::comma, ",");
    expect(lx.next().kind == SqlToken::Kind::ident, "name");
    expect(lx.next().kind == SqlToken::Kind::ident, "FROM");
    expect(lx.next().kind == SqlToken::Kind::ident, "enchantment");
    expect(lx.next().kind == SqlToken::Kind::ident, "WHERE");
    expect(lx.next().kind == SqlToken::Kind::ident, "max_level");
    expect(lx.next().kind == SqlToken::Kind::eq, "=");
    SqlToken t_int = lx.next();
    expect(t_int.kind == SqlToken::Kind::int_ && t_int.ival == 5, "5");
    expect(lx.next().kind == SqlToken::Kind::semi, ";");
    expect(lx.next().kind == SqlToken::Kind::end, "end");
    TEST_PASS("lexer basics");
}

TEST_CASE("sql_lexer_strings_and_comments") {
    SqlLexer lx("-- comment\nINSERT INTO tags (id, values) VALUES ('#a:b', 'x,y');");
    auto t1 = lx.next();
    expect(t1.kind == SqlToken::Kind::ident && t1.text == "INSERT", "comment skipped");
    while (lx.next().kind != SqlToken::Kind::end) {
    }
    SqlLexer l2("SELECT * FROM x WHERE id='it''s';");
    // '' 转义
    bool found = false;
    for (SqlToken t = l2.next(); t.kind != SqlToken::Kind::end; t = l2.next())
        if (t.kind == SqlToken::Kind::str && t.text == "it's")
            found = true;
    expect(found, "escaped quote");
    TEST_PASS("strings and comments");
}

TEST_CASE("sql_parser_statements") {
    auto stmts = SqlParser{}.parse("SELECT id, max_level FROM enchantment WHERE is_treasure=false ORDER BY id LIMIT 5 OFFSET 1;"
                                   "UPDATE equipment SET max_durability=500 WHERE id='minecraft:sword';"
                                   "DELETE FROM tags WHERE id='#x:y';"
                                   "INSERT INTO enchantment (id, name, max_level) VALUES ('a:b','AB',3);"
                                   "STATUS;SAVE ALL;");
    expect(stmts.size() == 6, "six statements");
    const auto& s0 = std::get<SelectStmt>(stmts[0]);
    expect(s0.table == "enchantment" && s0.cols.size() == 2 && s0.star == false, "select cols");
    expect(s0.where.size() == 1 && s0.where[0].col == "is_treasure" && s0.where[0].val == "false", "where");
    expect(s0.order_by == "id" && s0.limit == 5 && s0.offset == 1, "order/limit/offset");
    expect(std::get<UpdateStmt>(stmts[1]).sets[0].first == "max_durability", "update sets");
    expect(std::get<DeleteStmt>(stmts[2]).table == "tags", "delete");
    const auto& ins = std::get<InsertStmt>(stmts[3]);
    expect(ins.cols.size() == 3 && ins.vals[2] == "3", "insert");
    expect(std::holds_alternative<StatusStmt>(stmts[4]), "status");
    expect(std::get<SaveStmt>(stmts[5]).all, "save all");
    TEST_PASS("parser statements");
}

TEST_CASE("sql_parser_lexer_error_propagates") {
    // 未闭合字符串：lexer 置 error 并返回 end token——必须上浮为解析错误
    // （否则 parse"成功"丢弃畸形尾部，破坏 run_sql 零执行保证）。
    SqlParser p;
    auto r = p.parse("SELECT id FROM enchantment; 'unterminated");
    expect(r.empty() && !p.error.empty(), "unterminated string -> parse error, zero statements");
    expect(p.error.find("lexer error") != std::string::npos, "error names the lexer failure");
    // 非法字符同理。
    SqlParser p2;
    auto r2 = p2.parse("SELECT * FROM enchantment; @");
    expect(r2.empty() && !p2.error.empty(), "unexpected char -> parse error, zero statements");
    TEST_PASS("lexer error propagation");
}

TEST_CASE("sql_lexer_bool_case_insensitive") {
    // spec §2.2：TRUE/FALSE 大小写不敏感——bool 关键字按小写文本比较并归一
    // 为小写 token 文本（执行器 parse_bool/哨兵只认 "true"/"false"）。
    SqlLexer lx("TRUE FALSE TrUe");
    auto t1 = lx.next();
    expect(t1.kind == SqlToken::Kind::bool_ && t1.text == "true", "TRUE lexes as bool true");
    auto t2 = lx.next();
    expect(t2.kind == SqlToken::Kind::bool_ && t2.text == "false", "FALSE lexes as bool false");
    auto t3 = lx.next();
    expect(t3.kind == SqlToken::Kind::bool_ && t3.text == "true", "mixed-case TrUe lexes as bool true");
    expect(lx.next().kind == SqlToken::Kind::end, "end");
    TEST_PASS("lexer bool case insensitive");
}

TEST_CASE("sql_parser_case_insensitive") {
    // 列名大小写不敏感（spec §2.2）：SELECT cols / UPDATE SET / WHERE / INSERT
    // cols 解析期归一为小写（ORDER BY 早已归一）。
    auto s = SqlParser{}.parse("SELECT MAX_LEVEL, ID FROM enchantment WHERE NAME='x' ORDER BY ID;");
    expect(s.size() == 1, "uppercase select parses");
    const auto& sel = std::get<SelectStmt>(s[0]);
    expect(sel.cols.size() == 2 && sel.cols[0] == "max_level" && sel.cols[1] == "id", "select cols lowercased");
    expect(sel.where.size() == 1 && sel.where[0].col == "name" && sel.where[0].val == "x", "where col lowercased");
    expect(sel.order_by == "id", "order by lowercased");

    auto u = SqlParser{}.parse("UPDATE ENCHANTMENT SET MAX_LEVEL=3 WHERE ID='minecraft:sharpness';");
    expect(u.size() == 1, "uppercase update parses");
    const auto& upd = std::get<UpdateStmt>(u[0]);
    expect(upd.sets.size() == 1 && upd.sets[0].first == "max_level", "set col lowercased");
    expect(upd.where.size() == 1 && upd.where[0].col == "id", "update where col lowercased");

    auto i = SqlParser{}.parse("INSERT INTO ENCHANTMENT (ID, NAME, MAX_LEVEL) VALUES ('a:b','AB',3);");
    expect(i.size() == 1, "uppercase insert parses");
    const auto& ins = std::get<InsertStmt>(i[0]);
    expect(ins.cols.size() == 3 && ins.cols[0] == "id" && ins.cols[2] == "max_level", "insert cols lowercased");

    // WHERE TRUE（大写）→ 哨兵（匹配全部行）
    auto t = SqlParser{}.parse("UPDATE enchantment SET name='x' WHERE TRUE;");
    expect(t.size() == 1, "uppercase TRUE parses");
    const auto& ut = std::get<UpdateStmt>(t[0]);
    expect(ut.where.size() == 1 && ut.where[0].col.empty() && ut.where[0].val == "true",
           "uppercase TRUE is match-all sentinel");
    TEST_PASS("parser case insensitive");
}

TEST_CASE("sql_parser_errors") {
    SqlParser p;
    auto r = p.parse("FROB x;");
    expect(r.empty() && !p.error.empty(), "unknown statement rejected");
    SqlParser p2;
    auto r2 = p2.parse("QUIT;");
    expect(r2.empty() && p2.error.find("unsupported") != std::string::npos, "QUIT unsupported in slice 2");
    SqlParser p3;
    auto r3 = p3.parse("SELECT FROM enchantment;");
    expect(r3.empty() && !p3.error.empty(), "missing cols rejected");
    SqlParser p4;
    auto r4 = p4.parse("UPDATE equipment SET max_durability=1;");
    expect(r4.empty() && !p4.error.empty(), "UPDATE without WHERE rejected");
    SqlParser p5;
    auto r5 = p5.parse("DELETE FROM tags;");
    expect(r5.empty() && !p5.error.empty(), "DELETE without WHERE rejected");
    SqlParser p6;
    auto r6 = p6.parse("UPDATE equipment SET max_durability=1 WHERE true;");
    expect(r6.size() == 1, "WHERE true accepted");
    const auto& u = std::get<UpdateStmt>(r6[0]);
    expect(u.where.size() == 1 && u.where[0].col.empty(), "match-all sentinel");
    TEST_PASS("parser errors");
}

TEST_CASE("sql_parser_cross_profile_statements") {
    auto stmts = SqlParser{}.parse(
        "USE a:b;"
        "COPY id, name FROM src INTO enchantment WHERE id='x:y';"
        "COPY * FROM src INTO equipment WITH DEPS, OVERRIDE;"
        "MERGE INTO dest FROM src;"
        "FORK src AS new_p;");
    expect(stmts.size() == 5, "five statements");
    expect(std::get<UseStmt>(stmts[0]).profile == "a:b", "use profile with colon");
    const auto& c1 = std::get<CopyStmt>(stmts[1]);
    expect(c1.source == "src" && c1.table == "enchantment" && c1.cols.size() == 2 && !c1.star, "copy cols");
    expect(c1.where.size() == 1 && c1.where[0].col == "id" && c1.where[0].val == "x:y", "copy where");
    const auto& c2 = std::get<CopyStmt>(stmts[2]);
    expect(c2.star && c2.with_deps && c2.with_override && !c2.with_ignore, "copy mods");
    const auto& m = std::get<MergeStmt>(stmts[3]);
    expect(m.dest == "dest" && m.source == "src", "merge dest/src");
    const auto& f = std::get<ForkStmt>(stmts[4]);
    expect(f.source == "src" && f.dest == "new_p", "fork");
    TEST_PASS("cross-profile statements");
}

TEST_CASE("sql_parser_copy_mods_errors") {
    SqlParser p1;
    auto r1 = p1.parse("COPY * FROM a INTO tags WITH DEPS, IGNORE;");
    expect(r1.empty() && p1.error.find("mutually exclusive") != std::string::npos, "deps+ignore");
    SqlParser p2;
    auto r2 = p2.parse("COPY * FROM a INTO tags WITH BOGUS;");
    expect(r2.empty() && p2.error.find("unknown WITH modifier") != std::string::npos, "unknown mod");
    SqlParser p3;
    auto r3 = p3.parse("COPY FROM a INTO tags;");
    expect(r3.empty() && !p3.error.empty(), "missing cols");
    SqlParser p4;
    auto r4 = p4.parse("MERGE dest FROM src;");
    expect(r4.empty() && p4.error.find("INTO") != std::string::npos, "merge missing INTO");
    SqlParser p5;
    auto r5 = p5.parse("FORK a b;");
    expect(r5.empty() && p5.error.find("AS") != std::string::npos, "fork missing AS");
    SqlParser p6;
    auto r6 = p6.parse("USE a; FORK b AS c; COPY x FROM p INTO tags WITH OVERRIDE, OVERRIDE;");
    // slice 1 语义：语句级错误上浮——错误语句不产出，先前语句保留（仅 lexer 错误清空全部）。
    expect(p6.error.find("duplicate WITH") != std::string::npos, "duplicate mod");
    expect(r6.size() == 2, "earlier statements kept, bad COPY dropped");
    TEST_PASS("copy mods errors");
}
