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
    // （C1 回归门：显式 max_level/multiplier 的 INSERT → SAVE → reload 行存活；
    //   旧代码对缺省 0/0 的 INSERT 报"saved"但 reload 静默丢行。）
    ProfileManager mgr2;
    mgr2.load_directory(tp.dir);
    const Profile* p = mgr2.find("p1");
    expect(p != nullptr, "reloaded profile exists");
    if (p) {
        expect(p->ench().contains(NSID("test:sqlmark")), "enchantment survives reload");
        if (p->ench().contains(NSID("test:sqlmark"))) {
            expect_eq(p->ench().at(NSID("test:sqlmark")).max_level, 1, "enchantment max_level survives reload");
            expect_eq(p->ench().at(NSID("test:sqlmark")).multiplier, 1, "enchantment multiplier survives reload");
        }
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

// ─── SAVE 文件名消毒：含 ':' 的 key → 消毒路径写盘，name 字段保留真实 key ──

TEST_CASE("sql_session_save_windows_filename_sanitize") {
    auto tp = make_temp_profiles({"p1"});
    // ':' 是 Windows 非法文件名字符（profile key 任意字符串）——造一个有数据的
    // ':'-key profile（Profile API 给 tag，使 SQL INSERT 的 FK 引用可解析）。
    auto& p = tp.mgr.create("a:b");
    p.add_tag({NSID("#minecraft:swords"), "swords"});

    SqlSession s(tp.mgr, tp.dir);
    s.use("a:b");
    auto r = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                    "VALUES ('test:coloned','Coloned',1,1,'#minecraft:swords');");
    expect(r.affected == 1, "write on coloned key ok");
    auto sv = s.save(false);
    expect_eq(sv.message, "saved: a:b", "save message uses the real key");
    expect(s.dirty_profiles().empty(), "clean after save");

    // 消毒文件名 a_b.json 存在；原始 a:b.json 不得被创建
    const std::filesystem::path sanitized = std::filesystem::path(tp.dir) / "a_b.json";
    const std::filesystem::path raw = std::filesystem::path(tp.dir) / "a:b.json";
    expect(std::filesystem::exists(sanitized), "sanitized file exists");
    expect(!std::filesystem::exists(raw), "raw coloned filename not created");

    // 文件内顶层 name = 真实 key → load_directory 按 key 回读（与文件名无关）
    Json root = Json::parse(read_file_str(sanitized));
    expect_eq(root["name"].as<std::string>(), "a:b", "file name field keeps the real key");

    ProfileManager mgr2;
    mgr2.load_directory(tp.dir);
    const Profile* q = mgr2.find("a:b");
    expect(q != nullptr, "reloaded by key from the name field");
    if (q)
        expect(q->ench().contains(NSID("test:coloned")), "coloned-key profile content round-trips");

    tp.cleanup();
    TEST_PASS("save windows filename sanitize");
}

// ─── C2（终审）：文件名消毒碰撞守卫 ────────────────────────────────────
// 'a:b' 与 'a_b' 消毒后都映射到 a_b.json——后写者会静默覆盖前者。SAVE 必须
// 拒绝并指名两个 key；先写者的文件不得被覆盖。

TEST_CASE("sql_session_save_collision") {
    auto tp = make_temp_profiles({"p1"});
    auto& pa = tp.mgr.create("a:b");
    pa.add_tag({NSID("#minecraft:swords"), "swords"});
    auto& pb = tp.mgr.create("a_b");
    pb.add_tag({NSID("#minecraft:swords"), "swords"});

    SqlSession s(tp.mgr, tp.dir);
    s.use("a:b");
    auto r1 = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                     "VALUES ('test:ca','CA',1,1,'#minecraft:swords');");
    expect(r1.affected == 1, "write on a:b ok");
    s.use("a_b");
    auto r2 = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                     "VALUES ('test:cb','CB',1,1,'#minecraft:swords');");
    expect(r2.affected == 1, "write on a_b ok");
    expect(s.dirty_profiles() == (std::vector<std::string>{"a:b", "a_b"}), "both dirty");

    // SAVE ALL：排序后 a:b 先写 a_b.json（'a:b' < 'a_b'），a_b 撞同一文件名 →
    // 报错指名两个 key 与文件名
    auto sv = s.save(true);
    expect(sv.message.find("save collision") != std::string::npos, "collision error");
    expect(sv.message.find("a:b") != std::string::npos, "error names the first key");
    expect(sv.message.find("a_b") != std::string::npos, "error names the second key");
    expect(sv.message.find("a_b.json") != std::string::npos, "error names the colliding file");

    // 未静默覆盖：a_b.json 存在且内容 = a:b（先写者胜；后写者被拒）
    const std::filesystem::path f = std::filesystem::path(tp.dir) / "a_b.json";
    expect(std::filesystem::exists(f), "first file exists");
    if (std::filesystem::exists(f)) {
        Json root = Json::parse(read_file_str(f));
        expect_eq(root["name"].as<std::string>(), "a:b", "file holds the first key, not overwritten");
    }

    // a_b 保持脏；会话内再次 SAVE（单 key）同样被认领表拒绝（session-lifetime）
    expect(s.dirty_profiles() == std::vector<std::string>{"a_b"}, "colliding profile still dirty");
    s.use("a_b");
    auto sv2 = s.save(false);
    expect(sv2.message.find("save collision") != std::string::npos, "session-lifetime collision on later SAVE");
    expect(s.dirty_profiles() == std::vector<std::string>{"a_b"}, "still dirty after refused save");

    tp.cleanup();
    TEST_PASS("save collision");
}

// ─── I5（终审）：datapack 来源 profile 的 native SAVE 发警告 ─────────────
// datapack 目录加载的 profile 保存为 native JSON 后，reload 时与 datapack
// 目录构成双来源（同名文件 + 同名目录）→ 回读胜者不确定——SAVE 消息须带警告。

TEST_CASE("sql_session_save_datapack_warning") {
    // 最小 datapack：pack.mcmeta + 一个魔咒 + 引用 item tag
    static int dp_counter = 0;
    const std::string dp_name = "besq_sql_dp_" + std::to_string(++dp_counter);
    auto dp = std::filesystem::temp_directory_path() / dp_name;
    std::error_code ec;
    std::filesystem::remove_all(dp, ec);
    std::filesystem::create_directories(dp / "data" / "mytest" / "enchantment");
    std::filesystem::create_directories(dp / "data" / "minecraft" / "tags" / "item");
    {
        std::ofstream f(dp / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    {
        std::ofstream f(dp / "data" / "mytest" / "enchantment" / "leeching.json");
        f << R"({
            "description": "Leeching",
            "supported_items": "#minecraft:swords",
            "anvil_cost": 2,
            "max_level": 3,
            "min_cost": {"base": 5, "per_level_above_first": 5}
        })";
    }
    {
        std::ofstream f(dp / "data" / "minecraft" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }

    // profiles_dir：datapack 子目录（load_directory 会把带 pack.mcmeta 的目录
    // 加载为 datapack profile，key = 目录名）
    static int dir_counter = 0;
    auto profiles_dir = std::filesystem::temp_directory_path() / ("besq_sql_dp_dir_" + std::to_string(++dir_counter));
    std::filesystem::remove_all(profiles_dir, ec);
    std::filesystem::create_directories(profiles_dir);
    std::filesystem::copy(dp, profiles_dir / dp.filename(), std::filesystem::copy_options::recursive);

    ProfileManager mgr;
    mgr.load_directory(profiles_dir);
    const std::string key = dp.filename().string();
    expect(mgr.exists(key), "datapack profile loaded");
    expect(mgr.is_datapack_sourced(key), "manager marks datapack origin");
    expect(!mgr.is_datapack_sourced("builtin:vanilla"), "vanilla root is not datapack-sourced");

    SqlSession s(mgr, profiles_dir.string());
    s.use(key);
    auto w = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                    "VALUES ('mytest:extra','Extra',1,1,'#minecraft:swords');");
    expect(w.affected == 1, "write on datapack profile ok");
    auto sv = s.save(false);
    expect(sv.message.find("saved: " + key) != std::string::npos, "save succeeds");
    expect(sv.message.find("datapack-sourced") != std::string::npos, "warning names datapack origin");
    expect(sv.message.find(key) != std::string::npos, "warning names the profile");
    expect(s.dirty_profiles().empty(), "saved → clean");

    // 对照组：native profile 的 SAVE 不带警告
    auto tp = make_temp_profiles({"p1"});
    SqlSession s2(tp.mgr, tp.dir);
    s2.use("p1");
    run(s2, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
            "VALUES ('test:na','Na',1,1,'#minecraft:swords');");
    auto sv2 = s2.save(false);
    expect(sv2.message.find("datapack-sourced") == std::string::npos, "native save has no warning");
    tp.cleanup();

    std::filesystem::remove_all(dp, ec);
    std::filesystem::remove_all(profiles_dir, ec);
    TEST_PASS("save datapack warning");
}

// ─── 片 2：USE 语句分发（就地切换；成功 `use: <x>`，未知 → 消息不抛） ────
// USE 不标脏、不取基线、不压 undo 配对；其后写语句的脏跟踪作用于被 USE
// 的 profile（约束 10）。

TEST_CASE("sql_session_use_chain_dirty") {
    auto tp = make_temp_profiles({"p1", "p2"});
    SqlSession s(tp.mgr, tp.dir);
    s.use("p1");
    expect_eq(s.current(), "p1", "initial use p1");

    // USE 语句成功：消息 `use: <profile>`，affected=0，不标脏、不压写序
    auto u = run(s, "USE p2;");
    expect_eq(u.message, "use: p2", "use statement message");
    expect(u.affected == 0, "use affects 0 rows");
    expect(s.dirty_profiles().empty(), "USE does not mark dirty");
    expect_eq(s.current(), "p2", "current switched by USE");

    // USE 未知 profile → 语句错误消息（不抛异常），current 不变，无脏
    auto ub = run(s, "USE no_such_profile;");
    expect_eq(ub.message, "unknown profile 'no_such_profile'", "unknown use message");
    expect(ub.affected == 0, "failed use affects 0");
    expect_eq(s.current(), "p2", "failed USE keeps current");
    expect(s.dirty_profiles().empty(), "failed USE no dirty");

    // USE 后的写 → 脏跟踪作用于被 USE 的 profile；STATUS 显示它
    auto r = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                    "VALUES ('test:ux','UX',1,1,'#minecraft:swords');");
    expect(r.affected == 1, "insert on p2 ok");
    expect(s.dirty_profiles() == std::vector<std::string>{"p2"}, "dirty tracks the USE-switched profile");
    expect(s.status(StatusStmt{}).find("profile: p2 (dirty)") == 0, "STATUS shows the USE-switched profile");

    // USE 不产生 undo 配对：写序只有 [p2]（INSERT），UNDO 回滚 INSERT
    std::string err;
    expect(s.undo(err), "undo ok");
    expect(s.dirty_profiles() == std::vector<std::string>{"p2"}, "undo marks dirty (existing semantics)");

    tp.cleanup();
    TEST_PASS("use chain dirty");
}

// ─── 片 2：FORK 标脏 + 空基线 + SAVE ALL 持久化回读（约束 10） ──────────
// FORK 目标 = dest（不切换 current）；基线 = 空 Profile → STATUS <new> 全 +；
// SAVE ALL 同时持久化源与派生；load_directory 回读派生含源内容。

TEST_CASE("sql_session_fork_dirty_and_save") {
    auto tp = make_temp_profiles({"p1"});
    SqlSession s(tp.mgr, tp.dir);
    s.use("p1");
    // 源先有内容
    auto r0 = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                     "VALUES ('test:src','Src',1,1,'#minecraft:swords');");
    expect(r0.affected == 1, "source insert ok");

    auto f = run(s, "FORK p1 AS p2;");
    expect_eq(f.message, "forked: p2", "fork message");
    expect(f.affected == 1, "fork affects 1");
    expect_eq(s.current(), "p1", "FORK does not switch current");
    expect(s.dirty_profiles() == (std::vector<std::string>{"p1", "p2"}), "fork marks the new profile dirty");

    // STATUS p2：基线 = 空 Profile → 派生内容全 +
    const std::string st = s.status(StatusStmt{"p2", ""});
    expect(st.find("profile: p2 (dirty)") == 0, "status header for the fork target");
    expect(st.find("+test:src") != std::string::npos, "forked content shows as all-+");

    // SAVE ALL → p1/p2 文件都存在；load_directory 回读 p2 含源内容
    auto sv = s.save(true);
    expect(sv.message.find("saved: p1, p2") != std::string::npos, "save all message");
    expect(std::filesystem::exists(std::filesystem::path(tp.dir) / "p1.json"), "p1 file exists");
    expect(std::filesystem::exists(std::filesystem::path(tp.dir) / "p2.json"), "p2 file exists");
    expect(s.dirty_profiles().empty(), "save all cleans both");

    ProfileManager mgr2;
    mgr2.load_directory(tp.dir);
    const Profile* q = mgr2.find("p2");
    expect(q != nullptr, "forked profile survives reload");
    if (q)
        expect(q->ench().contains(NSID("test:src")), "forked content round-trips");

    tp.cleanup();
    TEST_PASS("fork dirty and save");
}

// ─── 片 2：FORK → UNDO 清理（约束 9/10） ───────────────────────────────
// UNDO 删除新 profile；配对名已不存在 → _dirty/_baselines 中该名被清除。

TEST_CASE("sql_session_fork_undo_cleanup") {
    auto tp = make_temp_profiles({"p1"});
    SqlSession s(tp.mgr, tp.dir);
    s.use("p1");
    auto f = run(s, "FORK p1 AS p2;");
    expect(f.affected == 1, "fork ok");
    expect(tp.mgr.find("p2") != nullptr, "forked profile exists in manager");
    expect(s.dirty_profiles() == std::vector<std::string>{"p2"}, "fork marks new profile dirty");

    std::string err;
    expect(s.undo(err), "undo ok");
    expect(tp.mgr.find("p2") == nullptr, "undo removed the forked profile");
    expect(s.dirty_profiles().empty(), "undo cleans the removed profile from dirty");
    expect(s.status(StatusStmt{"p2", ""}).find("unknown profile") != std::string::npos,
           "status of removed profile is unknown");

    tp.cleanup();
    TEST_PASS("fork undo cleanup");
}

// ─── 片 2：MERGE 目标标脏 + SAVE 持久化回读（约束 10） ──────────────────
// MERGE 目标 = dest（不切换 current）；affected>0 → dest 标脏 + 写序配对。

TEST_CASE("sql_session_merge_dirty") {
    auto tp = make_temp_profiles({"p1", "p2"});
    SqlSession s(tp.mgr, tp.dir);
    s.use("p1");
    auto r0 = run(s, "INSERT INTO enchantment (id, name, max_level, multiplier, supported_items) "
                     "VALUES ('test:msrc','MSrc',1,1,'#minecraft:swords');");
    expect(r0.affected == 1, "source insert ok");

    s.use("p2");
    auto m = run(s, "MERGE INTO p2 FROM p1;");
    expect(m.message.find("merged:") == 0, "merge message prefix");
    expect(m.affected >= 1, "merge affected > 0");
    expect_eq(s.current(), "p2", "current unchanged by merge");
    expect(s.dirty_profiles() == (std::vector<std::string>{"p1", "p2"}), "merge marks dest dirty");

    auto sv = s.save(true);
    expect(sv.message.find("saved: p1, p2") != std::string::npos, "save all persists both");

    ProfileManager mgr2;
    mgr2.load_directory(tp.dir);
    const Profile* q = mgr2.find("p2");
    expect(q != nullptr, "p2 survives reload");
    if (q)
        expect(q->ench().contains(NSID("test:msrc")), "merged row round-trips into p2");

    tp.cleanup();
    TEST_PASS("merge dirty");
}
