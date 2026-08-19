#define BESQ_TEST_MAIN
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/sql/SqlExecutor.h"
#include "domain/business/sql/SqlParser.h"
#include "domain/business/types/Profile.h"
#include "framework/test_framework.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace business::sql;

namespace {

/// Fixture: 与查询面测试一致——ProfileManager + 内建 vanilla 数据（含 TagResolver）。
ProfileManager make_vanilla() {
    ProfileManager mgr;
    ProfileLoader loader;
    auto& p = mgr.create("builtin:vanilla");
    loader.load_builtin(p);
    return mgr;
}

/// 解析（假定合法）并执行单条语句。
SqlResult run(SqlExecutor& ex, const std::string& sql) {
    auto stmts = SqlParser{}.parse(sql);
    return ex.execute(stmts[0]);
}

} // namespace

TEST_CASE("sql_write_insert_requires_id") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    const size_t before = p.ench().size();

    auto r = run(ex, "INSERT INTO enchantment (name) VALUES ('NoId');");
    expect(r.affected == 0, "missing id: no rows affected");
    expect(r.message.find("id") != std::string::npos, "missing id: error names the id column");
    expect(p.ench().size() == before, "missing id: no write");
    TEST_PASS("insert requires id");
}

TEST_CASE("sql_write_insert_fk_dangling") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    const size_t ench_before = p.ench().size();
    const size_t eq_before = p.eq().size();

    // exclusive_set：一个有效 + 一个悬空 → 拒绝且列出缺失（零半写入）
    auto r1 = run(
        ex,
        "INSERT INTO enchantment (id, name, exclusive_set) VALUES ('test:foo','Foo','minecraft:sharpness,minecraft:nope');");
    expect(r1.affected == 0, "dangling exclusive_set rejected");
    expect(r1.message.find("minecraft:nope") != std::string::npos, "lists the missing ref");
    expect(r1.message.find("exclusive_set") != std::string::npos, "names the column");
    expect(p.ench().size() == ench_before, "no half-write");
    expect(!p.ench().contains(NSID("test:foo")), "row not written");

    // supported_items：#tag 悬空 + 具体有效 → 只列悬空 #tag
    auto r2 = run(ex, "INSERT INTO enchantment (id, name, supported_items) VALUES "
                      "('test:foo','Foo','#minecraft:no_such_tag,minecraft:diamond_sword');");
    expect(r2.affected == 0, "dangling supported_items rejected");
    expect(r2.message.find("#minecraft:no_such_tag") != std::string::npos, "lists dangling tag ref");
    expect(r2.message.find("minecraft:diamond_sword") == std::string::npos, "valid ref not listed");
    expect(p.ench().size() == ench_before, "no write");

    // supported_items：具体物品悬空
    auto r3 =
        run(ex, "INSERT INTO enchantment (id, name, supported_items) VALUES ('test:foo','Foo','minecraft:no_such_item');");
    expect(r3.affected == 0 && r3.message.find("minecraft:no_such_item") != std::string::npos, "lists dangling concrete item");

    // equipment.category 悬空
    auto r4 = run(ex, "INSERT INTO equipment (id, name, category) VALUES ('test:item','Item','#minecraft:no_such_tag');");
    expect(r4.affected == 0, "dangling equipment category rejected");
    expect(r4.message.find("#minecraft:no_such_tag") != std::string::npos, "lists missing category tag");
    expect(p.eq().size() == eq_before, "no write");

    // tags.values 悬空（# 引用不可解析）
    auto r5 = run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:tag','tag','#minecraft:no_such_tag');");
    expect(r5.affected == 0, "dangling tag values rejected");
    expect(r5.message.find("#minecraft:no_such_tag") != std::string::npos, "lists unresolvable value");
    expect(!p.tags().contains(NSID("#test:tag")), "no write");
    TEST_PASS("insert fk dangling");
}

TEST_CASE("sql_write_insert_defaults_and_lists") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    // 缺失列取 Profile 构造默认
    auto r = run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:foo','Foo');");
    expect(r.affected == 1, "insert affected 1");
    expect(r.message.find("1 row") != std::string::npos, "affected message");
    auto sel = run(ex, "SELECT id, name, supported_platform, max_level, is_treasure, exclusive_set, supported_items FROM "
                       "enchantment WHERE id='test:foo';");
    expect(sel.rows.size() == 1, "row written");
    expect(sel.rows[0][2] == "none", "platform default");
    expect(sel.rows[0][3] == "0", "max_level default");
    expect(sel.rows[0][4] == "false", "treasure default");
    expect(sel.rows[0][5] == "", "exclusive_set default empty");
    expect(sel.rows[0][6] == "", "supported_items default empty");

    // 列表列：逗号拆分 → 逐项 NSID 化（'#' 保留）→ 读回排序拼接
    auto r2 = run(ex, "INSERT INTO enchantment (id, name, exclusive_set, supported_items) VALUES "
                      "('test:bar','Bar','minecraft:sharpness','#minecraft:swords,minecraft:diamond_sword');");
    expect(r2.affected == 1, "list insert ok");
    auto sel2 = run(ex, "SELECT exclusive_set, supported_items FROM enchantment WHERE id='test:bar';");
    expect(sel2.rows[0][0] == "minecraft:sharpness", "exclusive_set roundtrip");
    expect(sel2.rows[0][1] == "#minecraft:swords,minecraft:diamond_sword", "supported_items sorted roundtrip ('#' first)");
    TEST_PASS("insert defaults and lists");
}

TEST_CASE("sql_write_insert_duplicate_rejected") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    const size_t before = p.ench().size();
    auto r = run(ex, "INSERT INTO enchantment (id, name) VALUES ('minecraft:sharpness','X');");
    expect(r.affected == 0, "duplicate rejected");
    expect(!r.message.empty(), "error message");
    expect(p.ench().size() == before, "no write");
    TEST_PASS("insert duplicate rejected");
}

TEST_CASE("sql_write_update_reference_validation") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    // 合法改引用
    auto ok = run(ex, "UPDATE enchantment SET exclusive_set='minecraft:bane_of_arthropods' WHERE id='minecraft:sharpness';");
    expect(ok.affected == 1, "valid update affected 1");
    auto chk = run(ex, "SELECT exclusive_set FROM enchantment WHERE id='minecraft:sharpness';");
    expect(chk.rows[0][0] == "minecraft:bane_of_arthropods", "exclusive_set updated");

    // 悬空改引用 → 拒绝，原值保留
    auto bad =
        run(ex, "UPDATE enchantment SET exclusive_set='minecraft:sharpness,minecraft:nope' WHERE id='minecraft:sharpness';");
    expect(bad.affected == 0, "dangling update rejected");
    expect(bad.message.find("minecraft:nope") != std::string::npos, "lists missing ref");
    auto chk2 = run(ex, "SELECT exclusive_set FROM enchantment WHERE id='minecraft:sharpness';");
    expect(chk2.rows[0][0] == "minecraft:bane_of_arthropods", "unchanged on rejection");

    // equipment.category 悬空 → 拒绝，原值保留
    auto bad_eq = run(ex, "UPDATE equipment SET category='#minecraft:no_such_tag' WHERE id='minecraft:diamond_sword';");
    expect(bad_eq.affected == 0, "dangling equipment category rejected");
    expect(bad_eq.message.find("#minecraft:no_such_tag") != std::string::npos, "lists missing category tag");
    auto eq_chk = run(ex, "SELECT category FROM equipment WHERE id='minecraft:diamond_sword';");
    expect(eq_chk.rows[0][0] == "#minecraft:sword", "category unchanged");

    // tags.values 悬空 → 拒绝，原值保留
    auto bad_tag = run(ex, "UPDATE tags SET values='#minecraft:no_such_tag' WHERE id='#minecraft:enchantment/curse';");
    expect(bad_tag.affected == 0, "dangling tag values rejected");
    expect(bad_tag.message.find("#minecraft:no_such_tag") != std::string::npos, "lists unresolvable value");
    auto tag_chk = run(ex, "SELECT values FROM tags WHERE id='#minecraft:enchantment/curse';");
    expect(tag_chk.rows[0][0] == "minecraft:binding_curse,minecraft:vanishing_curse", "values unchanged");

    // tags.values 写 = REPLACE 语义（旧值整体替换）
    auto rep = run(ex, "UPDATE tags SET values='minecraft:binding_curse' WHERE id='#minecraft:enchantment/curse';");
    expect(rep.affected == 1, "values replace ok");
    auto tag_chk2 = run(ex, "SELECT values FROM tags WHERE id='#minecraft:enchantment/curse';");
    expect(tag_chk2.rows[0][0] == "minecraft:binding_curse", "REPLACE semantics: old values gone");

    // values='' 同样走 REPLACE：清空旧值
    auto clr = run(ex, "UPDATE tags SET values='' WHERE id='#minecraft:enchantment/curse';");
    expect(clr.affected == 1, "values clear ok");
    auto tag_chk3 = run(ex, "SELECT values FROM tags WHERE id='#minecraft:enchantment/curse';");
    expect(tag_chk3.rows[0][0] == "", "values cleared (REPLACE with empty)");
    TEST_PASS("update reference validation");
}

TEST_CASE("sql_write_delete_reverse_reference") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    // enchantment 被其它 exclusive_set 引用 → 拒绝并列来源
    auto r1 = run(ex, "DELETE FROM enchantment WHERE id='minecraft:sharpness';");
    expect(r1.affected == 0, "delete rejected");
    expect(r1.message.find("minecraft:sharpness") != std::string::npos, "names the target");
    expect(r1.message.find("exclusive_set") != std::string::npos, "names the referencing column");
    expect(p.ench().contains(NSID("minecraft:sharpness")), "still present");

    // 自插引用行后再删 equipment（supported_items 具体引用）→ 拒绝并列来源
    run(ex, "INSERT INTO enchantment (id, name, supported_items) VALUES "
            "('test:uses_sword','UsesSword','minecraft:diamond_sword');");
    auto r2 = run(ex, "DELETE FROM equipment WHERE id='minecraft:diamond_sword';");
    expect(r2.affected == 0, "equipment delete rejected");
    expect(r2.message.find("test:uses_sword") != std::string::npos, "lists the referencing enchantment");
    expect(p.eq().contains(NSID("minecraft:diamond_sword")), "still present");

    // 自插引用行后再删 tag（supported_items #引用）→ 拒绝并列来源
    run(ex, "INSERT INTO enchantment (id, name, supported_items) VALUES ('test:uses_tag','UsesTag','#minecraft:swords');");
    auto r3 = run(ex, "DELETE FROM tags WHERE id='#minecraft:swords';");
    expect(r3.affected == 0, "tag delete rejected");
    expect(r3.message.find("test:uses_tag") != std::string::npos, "lists the referencing enchantment");
    expect(p.tags().contains(NSID("#minecraft:swords")), "still present");
    TEST_PASS("delete reverse reference");
}

TEST_CASE("sql_write_delete_valid") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:del','Del');");
    auto r1 = run(ex, "DELETE FROM enchantment WHERE id='test:del';");
    expect(r1.affected == 1, "delete affected 1");
    expect(!p.ench().contains(NSID("test:del")), "row removed");

    run(ex, "INSERT INTO equipment (id, name, category) VALUES ('test:eqdel','EqDel','#minecraft:swords');");
    auto r2 = run(ex, "DELETE FROM equipment WHERE id='test:eqdel';");
    expect(r2.affected == 1, "equipment delete affected 1");
    expect(!p.eq().contains(NSID("test:eqdel")), "row removed");

    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:tagdel','tagdel','minecraft:sharpness');");
    auto r3 = run(ex, "DELETE FROM tags WHERE id='#test:tagdel';");
    expect(r3.affected == 1, "tag delete affected 1");
    expect(!p.tags().contains(NSID("#test:tagdel")), "row removed");

    // 无匹配 → 0 行
    auto r4 = run(ex, "DELETE FROM enchantment WHERE id='test:nope';");
    expect(r4.affected == 0, "no-match delete affected 0");
    expect(r4.message.find("0 row") != std::string::npos, "no-match message");
    TEST_PASS("delete valid");
}

TEST_CASE("sql_write_where_filtering") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:a','A');");
    run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:b','B');");
    run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:c','C');");

    // WHERE 精确匹配子集
    auto up = run(ex, "UPDATE enchantment SET name='Renamed' WHERE id='test:b';");
    expect(up.affected == 1, "update matched 1 row");
    auto nm = run(ex, "SELECT name FROM enchantment WHERE id='test:b';");
    expect(nm.rows[0][0] == "Renamed", "only target row updated");
    auto nm_a = run(ex, "SELECT name FROM enchantment WHERE id='test:a';");
    expect(nm_a.rows[0][0] == "A", "sibling row untouched");

    // WHERE true 哨兵 = 匹配全部
    const int64_t total = static_cast<int64_t>(p.ench().size());
    auto all = run(ex, "UPDATE enchantment SET name='RenamedAll' WHERE true;");
    expect(all.affected == total, "sentinel updates every row");

    // DELETE 按 WHERE 过滤
    auto cnt = run(ex, "DELETE FROM enchantment WHERE id='test:a';");
    expect(cnt.affected == 1, "delete filtered 1 row");
    expect(!p.ench().contains(NSID("test:a")), "row removed");
    TEST_PASS("write where filtering");
}

TEST_CASE("sql_write_atomicity_no_half_write") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    // INSERT：一个有效 + 一个无效引用 → 整句拒绝，零半写入，且不入 UNDO 栈
    const size_t before = p.ench().size();
    auto r = run(
        ex,
        "INSERT INTO enchantment (id, name, exclusive_set) VALUES ('test:foo','Foo','minecraft:sharpness,minecraft:nope');");
    expect(r.affected == 0, "rejected");
    expect(p.ench().size() == before, "no half-write");
    std::string err;
    expect(!ex.undo(err), "failed statement records no undo snapshot");

    // UPDATE 多行：引用变更先整体校验 → 任一行悬空则全部不写
    run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:u1','U1');");
    run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:u2','U2');");
    auto r2 = run(ex, "UPDATE enchantment SET exclusive_set='minecraft:sharpness,minecraft:nope' WHERE true;");
    expect(r2.affected == 0, "multi-row update rejected");
    auto chk = run(ex, "SELECT exclusive_set FROM enchantment WHERE id='test:u1';");
    expect(chk.rows[0][0] == "", "no row changed");
    auto chk2 = run(ex, "SELECT exclusive_set FROM enchantment WHERE id='test:u2';");
    expect(chk2.rows[0][0] == "", "no second row changed");

    // DELETE 多行：一行有反向引用 → 整句拒绝，零部分删除
    run(ex, "INSERT INTO enchantment (id, name, exclusive_set) VALUES ('test:x','X','test:u1');");
    auto r3 = run(ex, "DELETE FROM enchantment WHERE true;");
    expect(r3.affected == 0, "delete rejected");
    expect(p.ench().contains(NSID("test:u1")) && p.ench().contains(NSID("test:x")), "zero partial delete");
    TEST_PASS("write atomicity");
}

TEST_CASE("sql_undo_restores") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    std::string err;
    expect(!ex.undo(err), "empty stack: undo false");
    expect(!err.empty(), "empty stack: error set");

    run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:u1','U1');");
    run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:u2','U2');");
    expect(ex.undo(err), "first undo");
    expect(!p.ench().contains(NSID("test:u2")), "u2 reverted");
    expect(p.ench().contains(NSID("test:u1")), "u1 kept");
    expect(ex.undo(err), "second undo");
    expect(!p.ench().contains(NSID("test:u1")), "u1 reverted");
    expect(!ex.undo(err), "stack exhausted");

    // 失败语句不入栈：undo 仍回到上一次成功写
    run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:ok','Ok');");
    run(ex, "INSERT INTO enchantment (id, name, exclusive_set) VALUES ('test:bad','Bad','minecraft:nope');");
    expect(ex.undo(err), "undo skips failed statement");
    expect(!p.ench().contains(NSID("test:ok")), "reverted to pre-ok state");

    // tags.values 写同样可回滚（resolver 快照恢复）
    run(ex, "UPDATE tags SET values='minecraft:binding_curse' WHERE id='#minecraft:enchantment/curse';");
    auto v = run(ex, "SELECT values FROM tags WHERE id='#minecraft:enchantment/curse';");
    expect(v.rows[0][0] == "minecraft:binding_curse", "values replaced");
    expect(ex.undo(err), "undo values write");
    auto v2 = run(ex, "SELECT values FROM tags WHERE id='#minecraft:enchantment/curse';");
    expect(v2.rows[0][0] == "minecraft:binding_curse,minecraft:vanishing_curse", "values restored via resolver snapshot");
    TEST_PASS("undo restores");
}

TEST_CASE("sql_undo_fifo_cap") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    for (int i = 0; i < 17; ++i) {
        auto r = run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:u" + std::to_string(i) + "','U');");
        expect(r.affected == 1, "write " + std::to_string(i));
    }
    // 栈容量 16：最早一次写（u0）的快照被 FIFO 淘汰
    for (int i = 0; i < 16; ++i) {
        std::string err;
        expect(ex.undo(err), "undo " + std::to_string(i + 1));
    }
    expect(p.ench().contains(NSID("test:u0")), "oldest snapshot evicted: u0 present");
    expect(!p.ench().contains(NSID("test:u16")), "u16 reverted");
    std::string err;
    expect(!ex.undo(err), "17th undo fails");
    TEST_PASS("undo fifo cap");
}

TEST_CASE("sql_undo_cross_profile") {
    ProfileManager mgr = make_vanilla();
    Profile& p2 = mgr.create("p2");
    SqlExecutor ex(mgr, "profiles");

    ex.set_current("p2");
    auto r1 = run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:p2a','P2A');");
    expect(r1.affected == 1, "p2 insert");
    ex.set_current("builtin:vanilla");
    auto r2 = run(ex, "INSERT INTO enchantment (id, name) VALUES ('test:va','VA');");
    expect(r2.affected == 1, "vanilla insert");

    // 最近一次成功写 = vanilla 上的写
    std::string err;
    expect(ex.undo(err), "undo restores most recent (vanilla)");
    expect(!mgr.find("builtin:vanilla")->ench().contains(NSID("test:va")), "vanilla write reverted");
    expect(p2.ench().contains(NSID("test:p2a")), "p2 write kept");
    expect(ex.undo(err), "second undo restores p2");
    expect(!p2.ench().contains(NSID("test:p2a")), "p2 write reverted");
    TEST_PASS("undo cross profile");
}

TEST_CASE("sql_write_unknown_column_and_deferred") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    auto r1 = run(ex, "INSERT INTO enchantment (bogus) VALUES ('x');");
    expect(r1.affected == 0 && r1.message.find("bogus") != std::string::npos, "unknown insert column");

    auto r2 = run(ex, "UPDATE enchantment SET bogus=1 WHERE id='x';");
    expect(r2.affected == 0 && r2.message.find("bogus") != std::string::npos, "unknown set column");

    auto r3 = run(ex, "UPDATE enchantment SET id='minecraft:foo' WHERE id='x';");
    expect(r3.affected == 0 && r3.message.find("id") != std::string::npos, "primary key update rejected");

    auto r4 = run(ex, "DELETE FROM enchantment WHERE bogus='x';");
    expect(r4.affected == 0 && r4.message.find("bogus") != std::string::npos, "unknown where column");

    auto st = run(ex, "STATUS;");
    expect(st.message.find("Task 4") != std::string::npos, "STATUS deferred to Task 4");
    auto sv = run(ex, "SAVE;");
    expect(sv.message.find("Task 4") != std::string::npos, "SAVE deferred to Task 4");
    TEST_PASS("unknown columns and deferred statements");
}
