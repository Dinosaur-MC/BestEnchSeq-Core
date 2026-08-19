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

TEST_CASE("sql_parser_errors") {
    SqlParser p;
    auto r = p.parse("FROB x;");
    expect(r.empty() && !p.error.empty(), "unknown statement rejected");
    SqlParser p2;
    auto r2 = p2.parse("USE x;");
    expect(r2.empty() && p2.error.find("unsupported") != std::string::npos, "USE unsupported in slice 1");
    SqlParser p3;
    auto r3 = p3.parse("SELECT FROM enchantment;");
    expect(r3.empty() && !p3.error.empty(), "missing cols rejected");
    SqlParser p4;
    auto r4 = p4.parse("UPDATE x SET a=1;"); // 无 WHERE
    expect(r4.empty(), "UPDATE without WHERE rejected at parse (slice1 rule)");
    TEST_PASS("parser errors");
}
