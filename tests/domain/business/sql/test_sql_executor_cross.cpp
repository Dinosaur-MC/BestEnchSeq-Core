#define BESQ_TEST_MAIN
#include "domain/business/components/TagResolver.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/sql/SqlExecutor.h"
#include "domain/business/sql/SqlParser.h"
#include "domain/business/types/Profile.h"
#include "framework/test_framework.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace business::sql;

namespace {

/// 解析并执行单条语句。解析失败（如未知表）→ 返回语句错误消息（与 run_sql 的
/// 整体解析先行语义一致：零执行），避免对空结果取 [0]。
SqlResult run(SqlExecutor& ex, const std::string& sql) {
    SqlParser parser;
    auto stmts = parser.parse(sql);
    if (stmts.empty()) {
        SqlResult r;
        r.message = parser.error.empty() ? "parse error" : parser.error;
        return r;
    }
    return ex.execute(stmts[0]);
}

/// 双 profile fixture：src = FK 连通小数据集（2 ench / 2 eq / 2 tag 含 #ref 链）；
/// dst = 不相交数据集（1 ench / 1 eq / 1 tag）。单个用例按需向 dst 补行。
///
///   src.tags:     #test:weapons {minecraft:test_sword}   #test:all {#test:weapons}
///   src.equipment: minecraft:test_sword (cat #test:weapons)  minecraft:test_axe (cat #test:weapons)
///   src.enchantment: test:sword_ench (supp #test:weapons)
///                    test:sword_ench2 (supp minecraft:test_sword, excl test:sword_ench)
///   dst.tags:     #test:armor {minecraft:test_helmet}
///   dst.equipment: minecraft:test_helmet (cat #test:armor)
///   dst.enchantment: test:armor_ench (supp #test:armor)
ProfileManager make_src_dst() {
    ProfileManager mgr;
    mgr.create("src");
    mgr.create("dst");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("src");
    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:all','All','#test:weapons');");
    run(ex, "INSERT INTO equipment (id, name, category, max_durability) VALUES "
            "('minecraft:test_sword','Test Sword','#test:weapons',500);");
    run(ex, "INSERT INTO equipment (id, name, category, max_durability) VALUES "
            "('minecraft:test_axe','Test Axe','#test:weapons',600);");
    run(ex, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) VALUES "
            "('test:sword_ench','Sword Ench',3,2,'#test:weapons');");
    run(ex, "INSERT INTO enchantment (id, name, max_level, multiplier, exclusive_set, supported_items) VALUES "
            "('test:sword_ench2','Sword Ench 2',5,1,'test:sword_ench','minecraft:test_sword');");
    ex.set_current("dst");
    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:armor','Armor','minecraft:test_helmet');");
    run(ex, "INSERT INTO equipment (id, name, category, max_durability) VALUES "
            "('minecraft:test_helmet','Test Helmet','#test:armor',400);");
    run(ex, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) VALUES "
            "('test:armor_ench','Armor Ench',2,2,'#test:armor');");
    return mgr;
}

/// DEPS 测试专用：src = FK 链（ench→tag→equipment）+ tag→tag 环（直改 Profile +
/// resolver，SQL INSERT 的 FK 校验禁止建环）+ 环引用 ench；dst 空。
ProfileManager make_deps_pair() {
    ProfileManager mgr;
    mgr.create("src");
    mgr.create("dst");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("src");
    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:all','All','#test:weapons');");
    run(ex, "INSERT INTO equipment (id, name, category, max_durability) VALUES "
            "('minecraft:test_sword','Test Sword','#test:weapons',500);");
    run(ex, "INSERT INTO equipment (id, name, category, max_durability) VALUES "
            "('minecraft:test_axe','Test Axe','#test:weapons',600);");
    run(ex, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) VALUES "
            "('test:sword_ench','Sword Ench',3,2,'#test:weapons');");
    run(ex, "INSERT INTO enchantment (id, name, max_level, multiplier, exclusive_set, supported_items) VALUES "
            "('test:sword_ench2','Sword Ench 2',5,1,'test:sword_ench','minecraft:test_sword');");
    // tag→tag 环（#test:la ↔ #test:lb）+ 引用环的 ench：直改 Profile（SQL FK 禁建环）。
    Profile& src = *mgr.find("src");
    src.add_tag(EquipmentTag(NSID("#test:la"), "LoopA"));
    src.add_tag(EquipmentTag(NSID("#test:lb"), "LoopB"));
    auto res = src.tag_resolver_ptr();
    if (!res) {
        res = std::make_shared<TagResolver>();
        src.set_tag_resolver(res);
    }
    res->add_tag("test:la", {"#test:lb"});
    res->add_tag("test:lb", {"#test:la"});
    src.add_enchantment(EnchInfo(NSID("test:loop_ench"), "Loop Ench", MCE::None, 1, 0, 1, false, std::unordered_set<NSID>{},
                                 std::unordered_set<NSID>{NSID("#test:la")}));
    return mgr;
}

} // namespace

TEST_CASE("sql_copy_basic") {
    ProfileManager mgr;
    mgr.create("src");
    mgr.create("dst");
    SqlExecutor ex(mgr, "profiles");
    // src 自带引用目标（tag + equipment），再插引用它们的 ench。
    ex.set_current("src");
    auto st = run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
    expect(st.affected == 1, "src tag fixture");
    auto se = run(ex, "INSERT INTO equipment (id, name, category, max_durability) VALUES "
                      "('minecraft:test_sword','Test Sword','#test:weapons',500);");
    expect(se.affected == 1, "src equipment fixture");
    auto sa = run(ex, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) VALUES "
                      "('test:a','Ench A',3,2,'#test:weapons');");
    expect(sa.affected == 1, "src enchantment a fixture");
    auto sb = run(ex, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) VALUES "
                      "('test:b','Ench B',4,1,'minecraft:test_sword');");
    expect(sb.affected == 1, "src enchantment b fixture");
    // dst 预置相同引用目标（严格档校验宇宙 = 目标 profile）。
    ex.set_current("dst");
    auto dt = run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
    expect(dt.affected == 1, "dst tag fixture");
    auto de = run(ex, "INSERT INTO equipment (id, name, category, max_durability) VALUES "
                      "('minecraft:test_sword','Test Sword','#test:weapons',500);");
    expect(de.affected == 1, "dst equipment fixture");
    const size_t before = mgr.find("dst")->ench().size();

    const auto r = run(ex, "COPY * FROM src INTO enchantment;");
    expect(r.affected == 2, "basic copy affects 2 main rows");
    expect(r.message.find("2 row") != std::string::npos, "affected message");
    expect(mgr.find("dst")->ench().size() == before + 2, "target grew by 2");

    auto sel = run(ex, "SELECT id, name FROM enchantment WHERE id='test:a';");
    expect(sel.rows.size() == 1 && sel.rows[0][1] == "Ench A", "first row visible");
    auto sel2 = run(ex, "SELECT id, name FROM enchantment WHERE id='test:b';");
    expect(sel2.rows.size() == 1 && sel2.rows[0][1] == "Ench B", "second row visible");
    auto sel3 = run(ex, "SELECT max_level, multiplier, supported_items FROM enchantment WHERE id='test:a';");
    expect(sel3.rows[0][0] == "3" && sel3.rows[0][1] == "2" && sel3.rows[0][2] == "#test:weapons", "values roundtrip");
    TEST_PASS("copy basic");
}

TEST_CASE("sql_copy_where_and_cols") {
    ProfileManager mgr = make_src_dst();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("dst");
    // dst 预置 src 引用目标，使严格档通过。
    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
    run(ex, "INSERT INTO equipment (id, name, category) VALUES ('minecraft:test_sword','TS','#test:weapons');");

    // WHERE 过滤：只复制 sword_ench 一行。
    auto r1 = run(ex, "COPY * FROM src INTO enchantment WHERE id='test:sword_ench';");
    expect(r1.affected == 1, "where filters to 1 row");
    auto sel1 = run(ex, "SELECT name FROM enchantment WHERE id='test:sword_ench';");
    expect(sel1.rows.size() == 1 && sel1.rows[0][0] == "Sword Ench", "filtered row copied");
    expect(!mgr.find("dst")->ench().contains(NSID("test:sword_ench2")), "sibling not copied");

    // 列子集：缺列 = 目标默认（exclusive_set/supported_platform/is_treasure 未列 → 默认）。
    auto r2 = run(ex, "COPY id, name, max_level, multiplier, supported_items FROM src INTO enchantment "
                      "WHERE id='test:sword_ench2';");
    expect(r2.affected == 1, "column subset copy ok");
    auto sel2 = run(ex, "SELECT id, supported_platform, max_level, is_treasure, exclusive_set FROM enchantment "
                        "WHERE id='test:sword_ench2';");
    expect(sel2.rows[0][0] == "test:sword_ench2", "id copied");
    expect(sel2.rows[0][1] == "none", "supported_platform default (col omitted)");
    expect(sel2.rows[0][2] == "5", "max_level copied");
    expect(sel2.rows[0][3] == "false", "is_treasure default (col omitted)");
    expect(sel2.rows[0][4] == "", "exclusive_set default empty (col omitted)");

    // equipment 列子集：缺 max_durability → 0。
    auto r3 = run(ex, "COPY id, name, category FROM src INTO equipment WHERE id='minecraft:test_axe';");
    expect(r3.affected == 1, "equipment subset copy ok");
    auto sel3 = run(ex, "SELECT id, name, max_durability FROM equipment WHERE id='minecraft:test_axe';");
    expect(sel3.rows[0][0] == "minecraft:test_axe" && sel3.rows[0][1] == "Test Axe" && sel3.rows[0][2] == "0",
           "max_durability default 0");

    // tags 的 values 复制（raw 保留 #ref 链）→ 目标 resolver 可见。
    auto r4 = run(ex, "COPY id, values FROM src INTO tags WHERE id='#test:all';");
    expect(r4.affected == 1, "tags values copy ok");
    auto sel4 = run(ex, "SELECT values FROM tags WHERE id='#test:all';");
    expect(sel4.rows[0][0] == "minecraft:test_sword", "values resolved via target resolver");
    TEST_PASS("copy where and cols");
}

TEST_CASE("sql_copy_strict_missing_refs") {
    ProfileManager mgr = make_src_dst();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("dst");
    const size_t before = mgr.find("dst")->ench().size();

    // 源行引用源内 tag/equipment/enchantment，dst 均无 → 全量缺失清单 + used by。
    auto r = run(ex, "COPY * FROM src INTO enchantment;");
    expect(r.affected == 0, "strict missing refs rejected");
    expect(r.message.find("missing refs:") != std::string::npos, "error uses missing refs prefix");
    expect(r.message.find("used by") != std::string::npos, "error lists user row");
    expect(r.message == "missing refs: #test:weapons (used by test:sword_ench.supported_items), "
                        "minecraft:test_sword (used by test:sword_ench2.supported_items), "
                        "test:sword_ench (used by test:sword_ench2.exclusive_set)",
           "full list, stable sorted order");
    expect(mgr.find("dst")->ench().size() == before, "zero writes on failure");
    expect(!mgr.find("dst")->ench().contains(NSID("test:sword_ench")), "row not written");

    // 反向：dst 预置 tag 后仅剩 equipment/enchantment 引用缺失（缺什么列什么）。
    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
    auto r2 = run(ex, "COPY * FROM src INTO enchantment;");
    expect(r2.affected == 0, "still missing refs");
    expect(r2.message.find("#test:weapons") == std::string::npos, "satisfied tag not listed");
    expect(r2.message.find("minecraft:test_sword") != std::string::npos, "missing equipment listed");
    expect(r2.message.find("test:sword_ench") != std::string::npos, "missing enchantment listed");
    TEST_PASS("strict missing refs");
}

TEST_CASE("sql_copy_default_conflict") {
    ProfileManager mgr = make_src_dst();
    {
        // 预置（独立 executor：不污染 COPY 后的 undo 栈断言）。
        SqlExecutor setup(mgr, "profiles");
        setup.set_current("dst");
        run(setup, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
        run(setup, "INSERT INTO equipment (id, name, category) VALUES ('minecraft:test_sword','TS','#test:weapons');");
        run(setup, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) VALUES "
                   "('test:sword_ench','Existing',9,9,'#test:weapons');");
    }
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("dst");
    const size_t before = mgr.find("dst")->ench().size();

    auto r = run(ex, "COPY * FROM src INTO enchantment WHERE id='test:sword_ench';");
    expect(r.affected == 0, "main-row conflict rejected by default");
    expect(r.message.find("already exists") != std::string::npos, "conflict error text");
    expect(mgr.find("dst")->ench().size() == before, "atomic rollback: no write");
    auto sel = run(ex, "SELECT name FROM enchantment WHERE id='test:sword_ench';");
    expect(sel.rows[0][0] == "Existing", "target row untouched");
    std::string err;
    expect(!ex.undo(err), "failed statement records no undo snapshot");
    TEST_PASS("default conflict");
}

TEST_CASE("sql_copy_override") {
    ProfileManager mgr = make_src_dst();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("dst");
    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
    run(ex, "INSERT INTO equipment (id, name, category) VALUES ('minecraft:test_sword','TS','#test:weapons');");
    run(ex, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) VALUES "
            "('test:sword_ench','Existing',9,9,'#test:weapons');");
    // 其它目标行引用 test:sword_ench —— OVERRIDE 保留 id，引用不受影响。
    run(ex, "INSERT INTO enchantment (id, name, max_level, multiplier, exclusive_set, supported_items) VALUES "
            "('test:ref','Ref',1,1,'test:sword_ench','#test:weapons');");

    auto r = run(ex, "COPY * FROM src INTO enchantment WHERE id='test:sword_ench' WITH OVERRIDE;");
    expect(r.affected == 1, "override replaces 1 row");
    auto sel = run(ex, "SELECT name, max_level FROM enchantment WHERE id='test:sword_ench';");
    expect(sel.rows[0][0] == "Sword Ench" && sel.rows[0][1] == "3", "values now from source");
    auto ref = run(ex, "SELECT exclusive_set FROM enchantment WHERE id='test:ref';");
    expect(ref.rows[0][0] == "test:sword_ench", "id preserved: referencing row stays valid");

    // 无冲突时 OVERRIDE 行为同默认（直接写入）。
    auto r2 = run(ex, "COPY * FROM src INTO enchantment WHERE id='test:sword_ench2' WITH OVERRIDE;");
    expect(r2.affected == 1, "no-conflict override acts like default");
    expect(mgr.find("dst")->ench().contains(NSID("test:sword_ench2")), "row written");
    TEST_PASS("override");
}

TEST_CASE("sql_copy_deps") {
    // ── 场景 1：2 层闭包（ench→tag→equipment）+ tag→tag 环 + exclusive_set 边 ──
    ProfileManager mgr = make_deps_pair();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("dst");
    auto r = run(ex, "COPY * FROM src INTO enchantment WITH DEPS;");
    // 主行 3（sword_ench / sword_ench2 / loop_ench）+ 依赖 4（#test:weapons、
    // #test:la、#test:lb、minecraft:test_sword）；#test:all 与 test_axe 无引用不入闭包。
    expect(r.affected == 7, "main 3 + deps 4");
    auto& dst = *mgr.find("dst");
    expect(dst.ench().contains(NSID("test:sword_ench")) && dst.ench().contains(NSID("test:sword_ench2")) &&
               dst.ench().contains(NSID("test:loop_ench")),
           "all main rows written");
    expect(dst.tags().contains(NSID("#test:weapons")), "dep tag pulled");
    expect(dst.tags().contains(NSID("#test:la")) && dst.tags().contains(NSID("#test:lb")), "cycle tags pulled");
    expect(dst.eq().contains(NSID("minecraft:test_sword")), "dep equipment pulled");
    expect(!dst.eq().contains(NSID("minecraft:test_axe")), "unreferenced equipment not copied");
    expect(!dst.tags().contains(NSID("#test:all")), "unreferenced tag not copied");

    // ── 场景 2：环（tag→tag）不死循环（专用 WHERE 只复制环引用 ench） ──
    ProfileManager mgr2 = make_deps_pair();
    SqlExecutor ex2(mgr2, "profiles");
    ex2.set_current("dst");
    auto r2 = run(ex2, "COPY * FROM src INTO enchantment WHERE id='test:loop_ench' WITH DEPS;");
    expect(r2.affected == 3, "1 main + 2 cycle deps");
    expect(mgr2.find("dst")->tags().contains(NSID("#test:la")) && mgr2.find("dst")->tags().contains(NSID("#test:lb")),
           "cycle closure completes");

    // ── 场景 3：依赖行目标已存在 → 跳过（不覆盖、不报错） ──
    ProfileManager mgr3 = make_deps_pair();
    SqlExecutor ex3(mgr3, "profiles");
    ex3.set_current("dst");
    run(ex3, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','ExistingWeapons','minecraft:test_sword');");
    auto r3 = run(ex3, "COPY * FROM src INTO enchantment WHERE id='test:sword_ench' WITH DEPS;");
    expect(r3.affected == 2, "1 main + 1 dep (existing dep tag skipped)");
    auto tag_chk = run(ex3, "SELECT name, values FROM tags WHERE id='#test:weapons';");
    expect(tag_chk.rows[0][0] == "ExistingWeapons", "existing dep row untouched");
    expect(mgr3.find("dst")->eq().contains(NSID("minecraft:test_sword")), "new dep equipment still pulled");

    // ── 场景 4：DEPS 下主行冲突仍报错 ──
    ProfileManager mgr4 = make_deps_pair();
    SqlExecutor ex4(mgr4, "profiles");
    ex4.set_current("dst");
    run(ex4, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
    run(ex4, "INSERT INTO equipment (id, name, category) VALUES ('minecraft:test_sword','TS','#test:weapons');");
    run(ex4, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) VALUES "
             "('test:sword_ench','Existing',9,9,'#test:weapons');");
    const size_t before = mgr4.find("dst")->ench().size();
    auto r4 = run(ex4, "COPY * FROM src INTO enchantment WHERE id='test:sword_ench' WITH DEPS;");
    expect(r4.affected == 0 && r4.message.find("already exists") != std::string::npos, "main conflict errors with deps");
    expect(mgr4.find("dst")->ench().size() == before, "no writes on deps conflict");
    TEST_PASS("copy deps");
}

TEST_CASE("sql_copy_ignore") {
    ProfileManager mgr = make_src_dst();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("dst");

    // 悬空引用（tag/equipment 缺失、#ref 不可解析）→ IGNORE 跳过 FK 校验，全部写入。
    auto r = run(ex, "COPY * FROM src INTO enchantment WITH IGNORE;");
    expect(r.affected == 2, "dangling enchantment writes allowed");
    expect(mgr.find("dst")->ench().contains(NSID("test:sword_ench")), "row written despite dangling refs");
    expect(mgr.find("dst")->ench().contains(NSID("test:sword_ench2")), "second row too");

    auto r2 = run(ex, "COPY * FROM src INTO equipment WITH IGNORE;");
    expect(r2.affected == 2, "dangling equipment category allowed");
    expect(mgr.find("dst")->eq().contains(NSID("minecraft:test_sword")), "equipment written");

    auto r3 = run(ex, "COPY * FROM src INTO tags WITH IGNORE;");
    expect(r3.affected == 2, "unresolvable #ref values allowed");
    auto sel = run(ex, "SELECT values FROM tags WHERE id='#test:all';");
    expect(sel.rows[0][0] == "minecraft:test_sword", "raw values copied into target resolver");
    TEST_PASS("copy ignore");
}

TEST_CASE("sql_copy_atomic_on_fk_fail") {
    ProfileManager mgr = make_src_dst();
    {
        SqlExecutor setup(mgr, "profiles");
        setup.set_current("dst");
        // 预置使 sword_ench（supp #test:weapons）本可单独通过——整句仍原子拒绝。
        run(setup, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
        run(setup, "INSERT INTO equipment (id, name, category) VALUES ('minecraft:test_sword','TS','#test:weapons');");
    }
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("dst");
    const size_t before = mgr.find("dst")->ench().size();

    auto r = run(ex, "COPY * FROM src INTO enchantment;");
    expect(r.affected == 0, "statement rejected atomically");
    expect(r.message.find("missing refs:") != std::string::npos, "missing refs reported");
    expect(mgr.find("dst")->ench().size() == before, "zero partial writes");
    expect(!mgr.find("dst")->ench().contains(NSID("test:sword_ench")), "passable row also not written");
    std::string err;
    expect(!ex.undo(err), "failed statement records no undo snapshot");
    TEST_PASS("copy atomic on fk fail");
}

TEST_CASE("sql_copy_subset_invariant") {
    ProfileManager mgr = make_src_dst();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("dst");
    run(ex, "INSERT INTO tags (id, name, values) VALUES ('#test:weapons','Weapons','minecraft:test_sword');");
    run(ex, "INSERT INTO equipment (id, name, category) VALUES ('minecraft:test_sword','TS','#test:weapons');");

    // 缺 max_level/multiplier → 默认 0 → 报错（镜像 loader 不变量）。
    auto r1 = run(ex, "COPY id, name, supported_items FROM src INTO enchantment WHERE id='test:sword_ench';");
    expect(r1.affected == 0 && r1.message.find("would be dropped on reload") != std::string::npos,
           "subset missing max_level/multiplier rejected");
    expect(r1.message.find("test:sword_ench") != std::string::npos, "error names the row");

    // 缺 supported_items → 空 → 报错。
    auto r2 = run(ex, "COPY id, name, max_level, multiplier FROM src INTO enchantment WHERE id='test:sword_ench';");
    expect(r2.affected == 0 && r2.message.find("would be dropped on reload") != std::string::npos,
           "subset missing supported_items rejected");
    expect(!mgr.find("dst")->ench().contains(NSID("test:sword_ench")), "no write on invariant failure");

    // 完整必要列 → 成功（严格 FK 同步通过）。
    auto r3 = run(ex, "COPY id, name, max_level, multiplier, supported_items FROM src INTO enchantment "
                      "WHERE id='test:sword_ench';");
    expect(r3.affected == 1, "valid subset ok");
    auto sel = run(ex, "SELECT id, name, max_level, multiplier FROM enchantment WHERE id='test:sword_ench';");
    expect(sel.rows[0][0] == "test:sword_ench" && sel.rows[0][1] == "Sword Ench" && sel.rows[0][2] == "3" &&
               sel.rows[0][3] == "2",
           "subset values roundtrip");

    // * 整行复制不触发（源行合法）。
    auto r4 = run(ex, "COPY * FROM src INTO enchantment WHERE id='test:sword_ench2';");
    expect(r4.affected == 1, "star copy never triggers invariant");
    TEST_PASS("copy subset invariant");
}

TEST_CASE("sql_copy_missing_profiles") {
    ProfileManager mgr = make_src_dst();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("dst");

    // 源缺失 → 语句错误（不抛异常）。
    auto r1 = run(ex, "COPY * FROM no_such_src INTO enchantment;");
    expect(r1.affected == 0 && r1.message.find("no_such_src") != std::string::npos, "unknown source profile");

    // 目标（current）缺失 → 语句错误。
    SqlExecutor ex2(mgr, "profiles");
    ex2.set_current("no_such_target");
    auto r2 = run(ex2, "COPY * FROM src INTO enchantment;");
    expect(r2.affected == 0 && r2.message.find("no_such_target") != std::string::npos, "unknown target profile");

    // 未知表 / 未知列。
    SqlExecutor ex3(mgr, "profiles");
    ex3.set_current("dst");
    auto r3 = run(ex3, "COPY * FROM src INTO bogus_table;");
    expect(r3.affected == 0 && r3.message.find("bogus_table") != std::string::npos, "unknown table");
    auto r4 = run(ex3, "COPY bogus_col FROM src INTO enchantment;");
    expect(r4.affected == 0 && r4.message.find("bogus_col") != std::string::npos, "unknown column");
    TEST_PASS("copy missing profiles");
}
