#define BESQ_TEST_MAIN
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/sql/SqlExecutor.h"
#include "domain/business/types/Profile.h"
#include "framework/test_framework.h"

#include <string>
#include <vector>

using namespace business::sql;

namespace {

/// Fixture: 与 acceptance 一致——构造 ProfileManager，装载内建 vanilla 数据，
/// 注册到 `builtin:vanilla` 键（ManagePipeline 同款 create + load_builtin 路径）。
ProfileManager make_vanilla() {
    ProfileManager mgr;
    ProfileLoader loader;
    auto& p = mgr.create("builtin:vanilla");
    loader.load_builtin(p);
    return mgr;
}

} // namespace

TEST_CASE("sql_select_enchantment") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    expect(p.ench().size() > 0, "vanilla enchantments loaded");

    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    auto r = ex.execute(std::get<SelectStmt>(
        SqlParser{}.parse("SELECT id, max_level FROM enchantment WHERE is_treasure=false ORDER BY id LIMIT 3;")[0]));
    expect(r.headers.size() == 2 && r.headers[0] == "id", "headers");
    expect(r.rows.size() == 3, "limit applied");
    expect(r.rows[0].size() == 2, "row width");
    expect(r.rows[0][0] == "minecraft:aqua_affinity", "first row ordered by id");
    expect(r.rows[0][1] == "1", "max_level stringified");
    TEST_PASS("select enchantment");
}

TEST_CASE("sql_select_star_and_show") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    auto star = ex.execute(std::get<SelectStmt>(SqlParser{}.parse("SELECT * FROM enchantment LIMIT 2;")[0]));
    expect(star.headers.size() == 11, "star expands to 11 enchantment columns");
    expect(star.rows.size() == 2, "star limit");
    expect(star.rows[0].size() == 11, "star row width");

    // 片 1 方言：SHOW 与 SELECT 一样要求 FROM（SHOW 置 star → 全列）。
    auto show_eq = ex.execute(std::get<SelectStmt>(SqlParser{}.parse("SHOW FROM equipment;")[0]));
    expect(show_eq.headers.size() == 4, "equipment headers");
    expect(show_eq.headers[0] == "id" && show_eq.headers[3] == "max_durability", "equipment header order");
    expect(show_eq.rows.size() == p.eq().size(), "SHOW returns all equipment");

    auto show_tags = ex.execute(std::get<SelectStmt>(SqlParser{}.parse("SHOW FROM tags;")[0]));
    expect(show_tags.headers.size() == 3 && show_tags.headers[2] == "values", "tags headers");
    expect(show_tags.rows.size() == p.tags().size(), "SHOW returns all tags");
    TEST_PASS("select star and show");
}

TEST_CASE("sql_where_filtering") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    // 数值列过滤
    auto r = ex.execute(std::get<SelectStmt>(SqlParser{}.parse("SELECT id, max_level FROM enchantment WHERE max_level=5;")[0]));
    int n5 = 0;
    for (const auto& [id, e] : p.ench().data())
        if (e.max_level == 5)
            ++n5;
    expect(r.rows.size() == static_cast<size_t>(n5), "max_level=5 count matches registry");
    for (const auto& row : r.rows)
        expect(row[1] == "5", "max_level stringified");

    // bool 列过滤（"true"/"false" 字符串化）
    auto rb = ex.execute(
        std::get<SelectStmt>(SqlParser{}.parse("SELECT id, is_treasure FROM enchantment WHERE is_treasure=true;")[0]));
    int nt = 0;
    for (const auto& [id, e] : p.ench().data())
        if (e.is_treasure)
            ++nt;
    expect(rb.rows.size() == static_cast<size_t>(nt), "is_treasure=true count matches registry");
    for (const auto& row : rb.rows)
        expect(row[1] == "true", "bool stringified true");
    TEST_PASS("where filtering");
}

TEST_CASE("sql_where_sentinel_matches_all") {
    ProfileManager mgr = make_vanilla();
    const auto& p = *mgr.find("builtin:vanilla");
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    auto r = ex.execute(std::get<SelectStmt>(SqlParser{}.parse("SELECT id FROM enchantment WHERE true;")[0]));
    expect(r.rows.size() == p.ench().size(), "sentinel WHERE true matches all rows");
    TEST_PASS("where sentinel");
}

TEST_CASE("sql_order_by") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    // 字符串列降序
    auto desc = ex.execute(std::get<SelectStmt>(SqlParser{}.parse("SELECT id FROM enchantment ORDER BY id DESC LIMIT 1;")[0]));
    expect(desc.rows.size() == 1 && desc.rows[0][0] == "minecraft:wind_burst", "desc by id");

    // 数值列降序（按 int 比较）
    auto num = ex.execute(
        std::get<SelectStmt>(SqlParser{}.parse("SELECT id, max_level FROM enchantment ORDER BY max_level DESC LIMIT 1;")[0]));
    expect(num.rows[0][1] == "5", "numeric desc max_level");

    // 数值列升序（按 int 比较：1 < 10 必须成立）
    auto asc = ex.execute(
        std::get<SelectStmt>(SqlParser{}.parse("SELECT id, max_level FROM enchantment ORDER BY max_level ASC LIMIT 1;")[0]));
    expect(asc.rows[0][1] == "1", "numeric asc max_level");
    TEST_PASS("order by");
}

TEST_CASE("sql_limit_offset") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    auto r = ex.execute(std::get<SelectStmt>(SqlParser{}.parse("SELECT id FROM enchantment ORDER BY id LIMIT 2 OFFSET 1;")[0]));
    expect(r.rows.size() == 2, "limit+offset applied after order");
    expect(r.rows[0][0] == "minecraft:bane_of_arthropods", "offset skips first row");
    TEST_PASS("limit offset");
}

TEST_CASE("sql_column_projection_and_lists") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");

    auto r = ex.execute(std::get<SelectStmt>(
        SqlParser{}.parse("SELECT id, exclusive_set FROM enchantment WHERE id='minecraft:bane_of_arthropods';")[0]));
    expect(r.rows.size() == 1, "one row for bane_of_arthropods");
    expect(r.rows[0][0] == "minecraft:bane_of_arthropods", "projected id");
    expect(r.rows[0][1] == "minecraft:breach,minecraft:density,minecraft:impaling,minecraft:sharpness,minecraft:smite",
           "exclusive_set sorted comma-joined");

    auto supported = ex.execute(std::get<SelectStmt>(
        SqlParser{}.parse("SELECT id, supported_items FROM enchantment WHERE id='minecraft:mending';")[0]));
    expect(supported.rows[0][1] == "#minecraft:enchantable/durability", "supported_items str()");
    TEST_PASS("column projection and lists");
}

TEST_CASE("sql_equipment_table") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    auto r = ex.execute(std::get<SelectStmt>(
        SqlParser{}.parse("SELECT id, name, category, max_durability FROM equipment WHERE id='minecraft:diamond_sword';")[0]));
    expect(r.rows.size() == 1, "one equipment row");
    expect(r.rows[0][0] == "minecraft:diamond_sword", "eq id");
    expect(r.rows[0][1] == "Diamond Sword", "eq name");
    expect(r.rows[0][2] == "#minecraft:sword", "eq category str()");
    expect(r.rows[0][3] == "1561", "eq max_durability stringified");
    TEST_PASS("equipment table");
}

TEST_CASE("sql_tags_table") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    auto r = ex.execute(std::get<SelectStmt>(
        SqlParser{}.parse("SELECT id, name, values FROM tags WHERE id='#minecraft:enchantment/curse';")[0]));
    expect(r.rows.size() == 1, "one tag row");
    expect(r.rows[0][0] == "#minecraft:enchantment/curse", "tag id");
    expect(r.rows[0][1] == "minecraft:enchantment/curse", "tag name");
    expect(r.rows[0][2] == "minecraft:binding_curse,minecraft:vanishing_curse", "tag values sorted");
    TEST_PASS("tags table");
}

TEST_CASE("sql_unknown_column_error") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    auto r = ex.execute(std::get<SelectStmt>(SqlParser{}.parse("SELECT bogus FROM enchantment;")[0]));
    expect(r.headers.empty() && r.rows.empty(), "empty result on error");
    expect(r.message.find("bogus") != std::string::npos, "error message names the column");
    TEST_PASS("unknown column error");
}

TEST_CASE("sql_unknown_profile_error") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("no_such_profile");
    auto r = ex.execute(std::get<SelectStmt>(SqlParser{}.parse("SELECT id FROM enchantment;")[0]));
    expect(r.headers.empty() && r.rows.empty(), "empty result on error");
    expect(r.message.find("no_such_profile") != std::string::npos, "error message names the profile");
    TEST_PASS("unknown profile error");
}

TEST_CASE("sql_write_statements_deferred") {
    ProfileManager mgr = make_vanilla();
    SqlExecutor ex(mgr, "profiles");
    ex.set_current("builtin:vanilla");
    auto stmts = SqlParser{}.parse("INSERT INTO enchantment (id, name) VALUES ('a:b','AB');"
                                   "UPDATE equipment SET max_durability=1 WHERE id='x';"
                                   "DELETE FROM tags WHERE id='#x:y';"
                                   "STATUS;"
                                   "SAVE ALL;");
    expect(stmts.size() == 5, "five write statements parsed");
    for (const auto& st : stmts) {
        auto r = ex.execute(st);
        expect(r.headers.empty() && r.rows.empty(), "no result rows for write");
        expect(r.message.find("Task 3") != std::string::npos, "write statements land in Task 3");
    }
    TEST_PASS("write statements deferred");
}
