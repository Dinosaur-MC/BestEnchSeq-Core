#define BESQ_TEST_MAIN
#include "domain/business/components/TagResolver.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/sql/SqlParser.h"
#include "domain/business/sql/SqlSession.h"
#include "domain/business/types/Profile.h"
#include "framework/test_framework.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace business::sql;

namespace {

/// 临时 profiles 目录夹具：写入 N 个最小 native JSON profile 并经
/// load_directory 装载（与生产路径一致；每个 profile 的 tag 注册表 =
/// vanilla tag 宇宙，故 '#minecraft:swords' 等引用可直接用）。
struct TempProfiles {
    std::string dir;
    ProfileManager mgr;
    void cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

TempProfiles make_temp_profiles(const std::vector<std::string>& names) {
    static int counter = 0;
    TempProfiles tp;
    tp.dir = (std::filesystem::temp_directory_path() / ("besq_sql_session_" + std::to_string(++counter))).string();
    std::filesystem::create_directories(tp.dir);
    for (const auto& n : names) {
        std::ofstream f(std::filesystem::path(tp.dir) / (n + ".json"));
        f << "{\"name\":\"" << n << "\",\"enchantments\":[],\"equipments\":[],\"tags\":{}}";
    }
    tp.mgr.load_directory(tp.dir);
    return tp;
}

/// 解析（假定合法）并执行单条语句。
SqlResult run(SqlSession& s, const std::string& sql) {
    return s.execute(SqlParser{}.parse(sql)[0]);
}

/// 读文件全文。
std::string read_file_str(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

// ─── 脏状态机：写→脏、SAVE→清、UNDO→脏；use() 校验 ─────────────────────

TEST_CASE("sql_session_use_and_dirty_machine") {
    auto tp = make_temp_profiles({"p1"});
    SqlSession s(tp.mgr, tp.dir);
    expect(s.current().empty(), "no current before use");

    expect_throws_as<std::runtime_error>([&] { s.use("no_such_profile"); }, "use() unknown profile throws");
    s.use("p1");
    expect_eq(s.current(), "p1", "current set by use");

    expect(s.dirty_profiles().empty(), "clean at session start");

    auto r = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                    "VALUES ('test:a','A',1,1,'#minecraft:swords');");
    expect(r.affected == 1, "insert ok");
    expect(s.dirty_profiles() == std::vector<std::string>{"p1"}, "write → dirty");
    expect(s.status(StatusStmt{}).find("profile: p1 (dirty)") == 0, "status header shows dirty");

    auto sv = s.save(false);
    expect_eq(sv.message, "saved: p1", "save message");
    expect(s.dirty_profiles().empty(), "save → clean");
    expect(s.status(StatusStmt{}).find("(dirty)") == std::string::npos, "status header clean after save");

    auto sv2 = s.save(false);
    expect_eq(sv2.message, "nothing to save", "second save: nothing");

    // 写 + UNDO → 标脏（spec：UNDO 后标脏），行回滚
    run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
           "VALUES ('test:b','B',1,1,'#minecraft:swords');");
    expect(s.dirty_profiles() == std::vector<std::string>{"p1"}, "dirty again after second write");
    std::string err;
    expect(s.undo(err), "undo ok");
    expect(s.dirty_profiles() == std::vector<std::string>{"p1"}, "undo → still dirty");
    auto sel = run(s, "SELECT id FROM enchantment WHERE id='test:b';");
    expect(sel.rows.empty(), "undo rolled back the row");

    // undo 后无实际差（基线 = SAVE 后状态）但按 spec 仍标脏
    const std::string st = s.status(StatusStmt{});
    expect(st.find("(dirty)") != std::string::npos, "undo keeps dirty flag");
    expect(st.find("(no changes)") != std::string::npos, "undo → no real diff vs baseline");

    // execute() 对 STATUS/SAVE 的就地分发
    auto r3 = s.execute(SqlParser{}.parse("STATUS")[0]);
    expect(r3.message.find("profile: p1") != std::string::npos, "execute(STATUS) dispatches to status");
    auto r4 = s.execute(SqlParser{}.parse("SAVE")[0]);
    expect(r4.message.find("saved") != std::string::npos, "execute(SAVE) dispatches to save");
    tp.cleanup();
    TEST_PASS("use and dirty machine");
}

// ─── STATUS diff：+ / ~（字段: 旧->新）/ -，三表，profile/table 过滤 ────

TEST_CASE("sql_session_status_diff") {
    auto tp = make_temp_profiles({"p1"});
    SqlSession s(tp.mgr, tp.dir);
    s.use("p1");

    // 新增：enchantment（基线 = 会话起点，空 profile）
    auto r1 = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                     "VALUES ('test:new1','New1',1,1,'#minecraft:swords');");
    expect(r1.affected == 1, "insert new enchantment");
    auto st1 = s.status(StatusStmt{});
    expect(st1.find("enchantment: +test:new1") != std::string::npos, "status shows +row");
    expect(st1.find("equipment: (no changes)") != std::string::npos, "equipment unchanged");
    expect(st1.find("tags: (no changes)") != std::string::npos, "tags unchanged");

    // SAVE → 基线重置为含 test:new1 的状态，随后的修改才表现为 ~
    expect_eq(s.save(false).message, "saved: p1", "save before modification");

    // 修改：字段级 旧->新
    auto r2 = run(s, "UPDATE enchantment SET max_level=3 WHERE id='test:new1';");
    expect(r2.affected == 1, "update max_level");
    auto st2 = s.status(StatusStmt{});
    expect(st2.find("~test:new1(max_level: 1->3)") != std::string::npos, "status shows field old->new");
    expect(st2.find("+test:new1") == std::string::npos, "no longer a plain +");

    // 列表字段修改：supported_items 排序后比较（p1 无 equipment，用 #tag 引用）
    auto r3 = run(s, "UPDATE enchantment SET supported_items='#minecraft:axes' WHERE id='test:new1';");
    expect(r3.affected == 1, "update supported_items");
    auto st3 = s.status(StatusStmt{});
    expect(st3.find("supported_items: #minecraft:swords->#minecraft:axes") != std::string::npos, "list field diff old->new");

    // 删除：vanilla 自由 tag（无反向引用）
    auto r4 = run(s, "DELETE FROM tags WHERE id='#minecraft:anvil';");
    expect(r4.affected == 1, "delete free vanilla tag");
    auto st4 = s.status(StatusStmt{});
    expect(st4.find("tags: -#minecraft:anvil") != std::string::npos, "status shows -row");
    expect(st4.find("enchantment: ~test:new1") != std::string::npos, "enchantment line still ~");

    // table 过滤：只出该表
    auto st_tags = s.status(StatusStmt{"", "tags"});
    expect(st_tags.find("tags:") != std::string::npos, "filtered line present");
    expect(st_tags.find("enchantment:") == std::string::npos, "other tables filtered out");

    // profile 过滤：显式命名
    auto st_p = s.status(StatusStmt{"p1", ""});
    expect(st_p.find("profile: p1") != std::string::npos, "explicit profile filter");

    // 未脏 profile（builtin:vanilla 自动创建，从未触碰）→ 无差
    auto st_v = s.status(StatusStmt{"builtin:vanilla", ""});
    expect(st_v.find("profile: builtin:vanilla") == 0, "vanilla header");
    expect(st_v.find("(no changes)") != std::string::npos, "untouched profile no changes");

    // 未知 profile → 报错
    auto st_bad = s.status(StatusStmt{"no_such_profile", ""});
    expect(st_bad.find("unknown profile") != std::string::npos, "unknown profile error");

    tp.cleanup();
    TEST_PASS("status diff");
}

// ─── SAVE 写盘 + tags values 组合 + load_directory 回读一致 ─────────────

TEST_CASE("sql_session_save_roundtrip") {
    auto tp = make_temp_profiles({"p1"});
    SqlSession s(tp.mgr, tp.dir);
    s.use("p1");

    // INSERT 一个附魔 + 一个带 values 的 tag
    auto r1 = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                     "VALUES ('test:sqlmark','SQLMark',1,1,'#minecraft:swords');");
    expect(r1.affected == 1, "enchantment insert ok");
    auto r2 = run(s, "INSERT INTO tags (id, name, values) VALUES "
                     "('#test:my_tag','my_tag','minecraft:sharpness,minecraft:swift_sneak');");
    expect(r2.affected == 1, "tag insert ok");
    expect(s.dirty_profiles() == std::vector<std::string>{"p1"}, "dirty before save");

    auto sv = s.save(false);
    expect_eq(sv.message, "saved: p1", "save message");
    expect(s.dirty_profiles().empty(), "clean after save");

    // 文件内容：tags 组合为对象（loader 原生格式 key → [values]），values 来自 resolver
    const std::string content = read_file_str(std::filesystem::path(tp.dir) / "p1.json");
    Json root = Json::parse(content);
    expect(root.has("tags") && root["tags"].type() == JsonType::Object, "saved file tags is an object");
    Json my_tag = root["tags"]["test:my_tag"];
    expect(my_tag.type() == JsonType::Array, "custom tag present in saved file");
    if (my_tag.type() == JsonType::Array) {
        std::unordered_set<std::string> got;
        for (const auto& e : my_tag.as_array())
            got.insert(e.as<std::string>());
        expect(got.count("minecraft:sharpness") == 1 && got.count("minecraft:swift_sneak") == 1,
               "tag values serialized into the file");
    }

    // load_directory 回读：附魔 + tag（含 values）都还在
    ProfileManager mgr2;
    mgr2.load_directory(tp.dir);
    const Profile* p = mgr2.find("p1");
    expect(p != nullptr, "reloaded profile exists");
    if (p) {
        expect(p->ench().contains(NSID("test:sqlmark")), "enchantment survives reload");
        if (p->ench().contains(NSID("test:sqlmark")))
            expect_eq(p->ench().at(NSID("test:sqlmark")).max_level, 1, "enchantment field survives reload");
        expect(p->tags().contains(NSID("#test:my_tag")), "tag row survives reload");
        const TagResolver* tr = p->tag_resolver();
        expect(tr != nullptr, "resolver attached after reload");
        if (tr) {
            const auto* raw = tr->raw_values("test:my_tag");
            expect(raw != nullptr, "tag values survive reload");
            if (raw) {
                std::unordered_set<std::string> got;
                for (const auto& v : *raw)
                    if (const auto* e = std::get_if<EntryRef>(&v))
                        got.insert(e->id);
                expect(got.count("minecraft:sharpness") == 1 && got.count("minecraft:swift_sneak") == 1,
                       "both tag values present after reload");
            }
        }
    }

    tp.cleanup();
    TEST_PASS("save roundtrip");
}

// ─── SAVE 失败保持脏；SAVE 当前 vs ALL；unsaved_warning ────────────────

TEST_CASE("sql_session_save_failure_keeps_dirty") {
    auto tp = make_temp_profiles({"p1"});
    // profiles_dir 指向不存在目录 → 写盘失败
    SqlSession s(tp.mgr, "besq_no_such_save_dir_xyz");
    s.use("p1");
    run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
           "VALUES ('test:a','A',1,1,'#minecraft:swords');");
    auto sv = s.save(false);
    expect(sv.affected == 0, "failed save: zero rows");
    expect(!sv.message.empty(), "failed save: error message");
    expect(s.dirty_profiles() == std::vector<std::string>{"p1"}, "failed save keeps dirty");
    tp.cleanup();
    TEST_PASS("save failure keeps dirty");
}

TEST_CASE("sql_session_save_current_vs_all_and_warning") {
    auto tp = make_temp_profiles({"p1", "p2"});
    SqlSession s(tp.mgr, tp.dir);
    s.use("p1");
    run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
           "VALUES ('test:a','A',1,1,'#minecraft:swords');");
    s.use("p2");
    run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
           "VALUES ('test:b','B',1,1,'#minecraft:swords');");

    expect(s.dirty_profiles() == (std::vector<std::string>{"p1", "p2"}), "both dirty");
    expect_eq(s.unsaved_warning(), "unsaved changes in: p1, p2 \u2014 run SAVE to persist", "warning lists both");

    // SAVE（当前 = p1，切回后）→ 只存 p1
    s.use("p1");
    auto sv = s.save(false);
    expect_eq(sv.message, "saved: p1", "save current only");
    expect(s.dirty_profiles() == std::vector<std::string>{"p2"}, "p2 still dirty");
    expect(s.save(false).message == "nothing to save", "current clean → nothing");

    // SAVE ALL → p2
    auto sv2 = s.save(true);
    expect_eq(sv2.message, "saved: p2", "save all saves remaining");
    expect(s.dirty_profiles().empty(), "all clean after SAVE ALL");
    expect(s.save(true).message == "nothing to save", "SAVE ALL again → nothing");
    expect(s.unsaved_warning().empty(), "warning empty when clean");

    tp.cleanup();
    TEST_PASS("save current vs all and warning");
}
