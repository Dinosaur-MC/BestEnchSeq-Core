#define BESQ_TEST_MAIN
#include "common/io/FileUtils.hpp"
#include "common/io/json.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/components/Serializer.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/Profile.h"
#include "framework/test_framework.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// ─── Helper: create an enchantment info for testing ─────────────────────

static EnchInfo make_ench(const std::string& id_str, const std::string& name, int max_level) {
    return EnchInfo{
        NSID(id_str), name, MCE::All, max_level, max_level, 1, false, std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
}

// ─── Test: Create Profile ───────────────────────────────────────────────

TEST_CASE("test_create_profile") {
    ProfileManager mgr;
    auto& p = mgr.create("test:profile_a");

    expect(mgr.exists("test:profile_a"), "profile should exist after create");
    expect(mgr.size() == 1, "manager size should be 1 after one create");

    // Verify profile can be modified via returned reference
    bool added = p.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));
    expect(added, "add_enchantment should succeed");
    expect(p.has_enchantment(NSID("minecraft:sharpness")), "profile should contain sharpness");
}

// ─── Test: Create From ──────────────────────────────────────────────────

TEST_CASE("test_create_from") {
    ProfileManager mgr;

    // Create profile A and add an enchantment
    auto& a = mgr.create("test:a");
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));

    // Create B from A
    auto& b = mgr.create_from("test:a", "test:b");

    // Verify B exists and has same enchantments as A
    expect(mgr.exists("test:b"), "B should exist after create_from");
    expect(mgr.size() == 2, "manager should have 2 profiles");
    expect(b.has_enchantment(NSID("minecraft:sharpness")), "B should inherit sharpness from A");
    expect(a.ench().size() == b.ench().size(), "A and B should have same enchantment count");

    // Verify independence: modify B, A unchanged
    b.add_enchantment(make_ench("minecraft:unbreaking", "Unbreaking", 3));
    expect(b.has_enchantment(NSID("minecraft:unbreaking")), "B should have unbreaking after add");
    expect(!a.has_enchantment(NSID("minecraft:unbreaking")), "A should NOT have unbreaking (independence)");
}

// ─── Test: Remove Profile ───────────────────────────────────────────────

TEST_CASE("test_remove_profile") {
    ProfileManager mgr;
    mgr.create("test:a");
    mgr.create("test:b");

    bool removed = mgr.remove("test:a");
    expect(removed, "remove should return true for existing profile");
    expect(mgr.size() == 1, "size should be 1 after removing one profile");
    expect(!mgr.exists("test:a"), "removed profile should not exist");
    expect(mgr.exists("test:b"), "remaining profile should still exist");
}

// ─── Test: Remove Nonexistent ───────────────────────────────────────────

TEST_CASE("test_remove_nonexistent") {
    ProfileManager mgr;
    mgr.create("test:a");

    bool removed = mgr.remove("test:nonexistent");
    expect(!removed, "remove should return false for nonexistent profile");
    expect(mgr.size() == 1, "size should remain unchanged");
}

// ─── Test: Activate and Active ──────────────────────────────────────────

TEST_CASE("test_activate_and_active") {
    ProfileManager mgr;
    auto& a = mgr.create("test:a");
    mgr.create("test:b");

    // Activate first profile
    mgr.activate("test:a");
    expect(mgr.active_name() == "test:a", "active name should be a");
    // active() returns the same profile reference
    expect(&mgr.active() == &a, "active() should return reference to profile a");

    // Activate second profile
    mgr.activate("test:b");
    expect(mgr.active_name() == "test:b", "active name should be b after switching");
}

// ─── Test: Activate Nonexistent Throws ──────────────────────────────────

TEST_CASE("test_activate_nonexistent_throws") {
    ProfileManager mgr;
    mgr.create("test:a");

    expect_throws_as<std::runtime_error>([&]() { mgr.activate("test:nonexistent"); },
                                         "activate() should throw for nonexistent profile");
}

// ─── Test: Empty Active Throws ──────────────────────────────────────────

TEST_CASE("test_empty_active_throws") {
    ProfileManager mgr; // no profiles

    expect_throws_as<std::runtime_error>([&]() { mgr.active(); }, "active() should throw when manager is empty");
}

// ─── Test: List ─────────────────────────────────────────────────────────

TEST_CASE("test_list") {
    ProfileManager mgr;
    mgr.create("test:alpha");
    mgr.create("test:beta");
    mgr.create("test:gamma");

    auto names = mgr.list();
    expect(names.size() == 3, "list should return 3 names");

    // Each of the expected names must be in the list
    bool found_alpha = false, found_beta = false, found_gamma = false;
    for (const auto& n : names) {
        if (n == "test:alpha")
            found_alpha = true;
        if (n == "test:beta")
            found_beta = true;
        if (n == "test:gamma")
            found_gamma = true;
    }
    expect(found_alpha, "list should contain alpha");
    expect(found_beta, "list should contain beta");
    expect(found_gamma, "list should contain gamma");
}

// ─── Test: Find ─────────────────────────────────────────────────────────

TEST_CASE("test_find") {
    ProfileManager mgr;
    mgr.create("test:found_me");

    Profile* p = mgr.find("test:found_me");
    expect(p != nullptr, "find() should return non-null for existing profile");

    Profile* q = mgr.find("test:unknown");
    expect(q == nullptr, "find() should return null for unknown profile");
}

// ─── Test: Snapshot ─────────────────────────────────────────────────────

TEST_CASE("test_snapshot") {
    ProfileManager mgr;

    // Create profile A with an enchantment
    auto& a = mgr.create("test:a");
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));

    // Snapshot
    auto& snap = mgr.snapshot("test:a", "test:a_snap");

    // Verify snapshot exists and has same data
    expect(mgr.exists("test:a_snap"), "snapshot should exist");
    expect(snap.has_enchantment(NSID("minecraft:sharpness")), "snapshot should inherit sharpness");
    expect(a.ench().size() == snap.ench().size(), "snapshot should have same enchantment count as original");

    // Modify A — add a new enchantment
    a.add_enchantment(make_ench("minecraft:protection", "Protection", 4));

    // Verify snapshot unchanged (immutable)
    expect(a.has_enchantment(NSID("minecraft:protection")), "A should have protection after add");
    expect(!snap.has_enchantment(NSID("minecraft:protection")), "snapshot should NOT have protection (immutable)");
    expect(a.ench().size() == snap.ench().size() + 1, "snapshot should have one fewer enchantment than A");
}

// ─── Test: Branch ───────────────────────────────────────────────────────

TEST_CASE("test_branch") {
    ProfileManager mgr;

    // Create profile A
    auto& a = mgr.create("test:a");
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));

    // Branch
    auto& branch = mgr.branch("test:a", "test:a_branch");

    // Verify branch exists and inherited data
    expect(mgr.exists("test:a_branch"), "branch should exist");
    expect(branch.has_enchantment(NSID("minecraft:sharpness")), "branch should inherit sharpness");

    // Modify branch — A unchanged
    branch.add_enchantment(make_ench("minecraft:unbreaking", "Unbreaking", 3));
    expect(branch.has_enchantment(NSID("minecraft:unbreaking")), "branch should have unbreaking");
    expect(!a.has_enchantment(NSID("minecraft:unbreaking")), "A should NOT have unbreaking (branch independence)");
}

// ─── Test: Merge ────────────────────────────────────────────────────────

TEST_CASE("test_merge") {
    ProfileManager mgr;

    // Create profile A with enchantment X (sharpness)
    auto& a = mgr.create("test:a");
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));

    // Create profile B with enchantment Y (unbreaking)
    auto& b = mgr.create("test:b");
    b.add_enchantment(make_ench("minecraft:unbreaking", "Unbreaking", 3));

    // Merge B into A
    mgr.merge("test:b", "test:a");

    // Verify A now has both X and Y
    expect(a.has_enchantment(NSID("minecraft:sharpness")), "A should have sharpness after merge");
    expect(a.has_enchantment(NSID("minecraft:unbreaking")), "A should have unbreaking after merge");
    expect(a.ench().size() == 2, "A should have 2 enchantments after merge");

    // B unchanged
    expect(b.has_enchantment(NSID("minecraft:unbreaking")), "B should still have unbreaking");
    expect(b.ench().size() == 1, "B should still have 1 enchantment");
}

// ─── Test: Merge with missing source/dest throws (I-2 regression) ────────

TEST_CASE("test_pm_merge_missing_throws") {
    ProfileManager pm;
    pm.create("base");

    expect_throws_as<std::runtime_error>([&]() { pm.merge("missing", "base"); }, "merge with missing source throws");
    expect_throws_as<std::runtime_error>([&]() { pm.merge("base", "missing"); }, "merge with missing dest throws");

    TEST_PASS("test_pm_merge_missing_throws");
}

// ─── Test: Create Empty Profile Structure ───────────────────────────────

TEST_CASE("test_create_empty_profile_structure") {
    ProfileManager mgr;
    auto& p = mgr.create("test:empty");

    // Check default metadata version
    expect(p.metadata().version.empty(), "default version should be empty string");
    expect(p.metadata().name == "test:empty", "name should match create parameter");
    expect(p.ench().size() == 0, "empty profile should have 0 enchantments");
    expect(p.eq().size() == 0, "empty profile should have 0 equipment");
    expect(p.tags().size() == 0, "empty profile should have 0 tags");
}

// ─── Test: Dependency Chain (transitive topological resolution) ──────

TEST_CASE("test_pm_dependency_chain") {
    ProfileManager pm;
    pm.create("builtin:vanilla");
    auto& mod = pm.create("enchantencore");
    mod.set_dependencies({"builtin:vanilla"});
    auto& pack = pm.create("mypack");
    pack.set_dependencies({"enchantencore"});

    auto chain = pm.resolve_dependencies("mypack");
    // transitive: mypack -> enchantencore -> builtin:vanilla; deps before self, self excluded
    expect(chain.size() == 2, "mypack has 2 deps (enchantencore + builtin:vanilla)");
    expect(chain[0] == "builtin:vanilla", "builtin:vanilla first (leaf dep)");
    expect(chain[1] == "enchantencore", "enchantencore second");

    TEST_PASS("test_pm_dependency_chain");
}

// ─── Test: Dependency Cycle Detection ────────────────────────────────

TEST_CASE("test_pm_dependency_cycle") {
    ProfileManager pm;
    auto& a = pm.create("a");
    auto& b = pm.create("b");
    a.set_dependencies({"b"});
    b.set_dependencies({"a"});

    auto chain = pm.resolve_dependencies("a");
    expect(chain.empty(), "cycle detected → empty chain");

    TEST_PASS("test_pm_dependency_cycle");
}

// ─── Test: dependency cycle → reported + resolve_effective throws (B-T26 #21) ──
// is_cyclic distinguishes a cycle from a not-found profile; resolve_effective
// throws on a cyclic profile instead of silently degrading to a self-only view.

TEST_CASE("test_pm_dependency_cycle_throws") {
    ProfileManager pm;
    auto& a = pm.create("a");
    auto& b = pm.create("b");
    a.set_dependencies({"b"});
    b.set_dependencies({"a"});

    expect(pm.is_cyclic("a"), "a is cyclic");
    expect(pm.is_cyclic("b"), "b is cyclic");
    expect(!pm.is_cyclic("nonexistent"), "nonexistent profile is NOT cyclic");
    expect_throws_as<std::runtime_error>([&]() { pm.resolve_effective("a"); }, "resolve_effective on a cyclic profile throws");

    TEST_PASS("test_pm_dependency_cycle_throws");
}

// ─── Test: Cross-validate supported_items against dependency universe ─

TEST_CASE("test_pm_cross_validate") {
    ProfileManager pm;
    auto& vanilla = pm.create("builtin:vanilla");
    vanilla.add_equipment(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID("#minecraft:sword"), 1561});
    vanilla.add_tag(EquipmentTag{NSID("#minecraft:sword"), "sword"});

    auto& mod = pm.create("mod");
    mod.set_dependencies({"builtin:vanilla"});

    // Valid refs (tag + concrete item from vanilla) plus one unknown item.
    EnchInfo sharp = make_ench("minecraft:sharpness", "Sharpness", 5);
    sharp.supported_items = {NSID("#minecraft:sword"), NSID("minecraft:diamond_sword"), NSID("minecraft:stone")};
    mod.add_enchantment(sharp);

    // All refs unknown → enchantment must be removed entirely.
    EnchInfo mending = make_ench("minecraft:mending", "Mending", 1);
    mending.supported_items = {NSID("minecraft:netherite_ingot")};
    mod.add_enchantment(mending);

    size_t removed = pm.cross_validate("mod");
    expect(removed == 2, "two dangling refs removed");

    expect(mod.has_enchantment(NSID("minecraft:sharpness")), "sharpness survives cross-validate");
    const auto& kept = mod.ench().at(NSID("minecraft:sharpness")).supported_items;
    expect(kept.size() == 2, "sharpness keeps the two valid refs");
    expect(kept.count(NSID("#minecraft:sword")) == 1, "tag ref kept");
    expect(kept.count(NSID("minecraft:diamond_sword")) == 1, "concrete item ref kept");
    expect(!mod.has_enchantment(NSID("minecraft:mending")), "mending dropped (no valid refs)");

    TEST_PASS("test_pm_cross_validate");
}

// ─── Test: Load Directory ────────────────────────────────────────────

TEST_CASE("test_pm_load_directory") {
    // Nonexistent directory is a safe no-op.
    ProfileManager pm;
    pm.load_directory(std::filesystem::temp_directory_path() / "besq_no_such_dir_xyz");
    expect(pm.size() == 0, "no profiles loaded from missing directory");

    // Temp directory with one native-JSON profile.
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / ("besq_pm_dir_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);
    auto path = dir / "bare_mod.json";
    {
        std::ofstream f(path);
        f << R"({
            "name": "bare_mod",
            "dependencies": ["builtin:vanilla"],
            "enchantments": [],
            "equipments": [],
            "categories": [],
            "tags": {}
        })";
    }

    pm.load_directory(dir);
    expect(pm.exists("bare_mod"), "bare_mod loaded by file stem");
    expect(pm.exists("builtin:vanilla"), "builtin:vanilla base auto-created");

    // The JSON `dependencies` array must be parsed into the loaded profile.
    Profile* loaded_mod = pm.find("bare_mod");
    expect(loaded_mod != nullptr, "loaded bare_mod findable");
    if (loaded_mod) {
        const auto& deps = loaded_mod->dependencies();
        expect(deps.size() == 1 && deps[0] == "builtin:vanilla", "dependencies parsed from JSON root");
    }

    // Cleanup temp files.
    std::filesystem::remove(path);
    std::filesystem::remove(dir);
    TEST_PASS("test_pm_load_directory");
}

// ─── Test: replace-on-conflict preserves the active selection ───────────
// load_directory 对同名文件做 remove+re-add（replace-on-conflict）；remove()
// 会清空/改指活动选中，重新 add 后必须恢复，否则替换活动 profile 时选中
// 丢失（SQL SAVE→reload 跨进程回读门暴露，Task 6 Bug 1）。

TEST_CASE("test_pm_load_directory_replace_keeps_active") {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / ("besq_pm_replace_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);
    auto path = dir / "bare_mod.json";
    auto write_mod = [&](const char* name) {
        std::ofstream f(path);
        f << R"({
            "name": "bare_mod",
            "dependencies": ["builtin:vanilla"],
            "enchantments": [],
            "equipments": [],
            "categories": [],
            "tags": {}
        })";
        (void)name;
    };

    ProfileManager pm;
    write_mod("first");
    pm.load_directory(dir);
    expect(pm.exists("bare_mod"), "bare_mod loaded");
    pm.activate("builtin:vanilla");  // CLI 流：load_builtin() 先激活根 profile
    expect(pm.active_name() == "builtin:vanilla", "builtin:vanilla active before replace");

    // 替换活动 profile（builtin:vanilla 自身被同名文件替换）→ 选中保持
    auto vp = dir / "builtin_vanilla.json";
    {
        std::ofstream f(vp);
        f << R"({
            "name": "builtin:vanilla",
            "dependencies": [],
            "enchantments": [],
            "equipments": [],
            "categories": [],
            "tags": {}
        })";
    }
    pm.load_directory(dir);
    expect(pm.exists("builtin:vanilla"), "builtin:vanilla re-added after replace");
    expect(pm.active_name() == "builtin:vanilla", "active selection preserved after replacing active profile");

    // 非活动 profile 被替换 → 活动选中不受影响
    pm.activate("bare_mod");
    write_mod("second");
    pm.load_directory(dir);
    expect(pm.active_name() == "bare_mod", "active selection preserved after replacing non-active profile");

    std::filesystem::remove(path);
    std::filesystem::remove(vp);
    std::filesystem::remove(dir);
    TEST_PASS("test_pm_load_directory_replace_keeps_active");
}

// ─── 积压清扫 T1.6（spec §3.1 测试补强批）：唯一活跃 profile 被替换 ───────
// 空 map 分支（片 1 T6 minor）——test_pm_load_directory_replace_keeps_active
// 覆盖 ≥2 profile 时的 replace 恢复；本用例补 remove() 的 `_profiles.empty()`
// 分支（L110-111：移除唯一 profile → _active.clear()）的直接测试，以及
// replace-only-active 走该分支后选中恢复。

TEST_CASE("test_pm_remove_only_active_clears_selection") {
    // 直接路径：创建唯一 profile → 激活 → 移除 → 活动选中清空（空 map 分支）。
    ProfileManager mgr;
    mgr.create("solo");
    mgr.activate("solo");
    expect(mgr.active_name() == "solo", "solo active");
    expect(mgr.remove("solo"), "remove the only active profile succeeds");
    expect(mgr.size() == 0, "manager empty after remove");
    expect(mgr.active_name().empty(), "removing the only active profile clears the active selection");
    expect_throws_as<std::runtime_error>([&]() { mgr.active(); },
                                         "active() throws when manager is empty after remove");
    expect_throws_as<std::runtime_error>([&]() { mgr.activate("solo"); },
                                         "activating a removed profile throws");

    // replace-only-active（load_directory replace-on-conflict 路径）：目录仅含
    // 一个 profile 且为活动选中 → remove() 走空 map 分支 → 重新 add 后恢复选中。
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / ("besq_pm_solo_replace_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);
    auto path = dir / "builtin_vanilla.json";
    {
        std::ofstream f(path);
        f << R"({
            "name": "builtin:vanilla",
            "dependencies": [],
            "enchantments": [],
            "equipments": [],
            "categories": [],
            "tags": {}
        })";
    }
    ProfileManager pm;
    pm.load_directory(dir);
    expect(pm.exists("builtin:vanilla"), "only profile loaded");
    expect(pm.size() == 1, "exactly one profile (empty-map branch precondition)");
    pm.activate("builtin:vanilla");
    // 重载同一目录 → 同名文件 replace-on-conflict：remove() 后 map 空
    // （空 map 分支 _active.clear()），重新 add 后活动选中必须恢复。
    pm.load_directory(dir);
    expect(pm.exists("builtin:vanilla"), "re-added after replacing the ONLY active profile");
    expect(pm.active_name() == "builtin:vanilla", "active restored after replacing the ONLY active profile");

    std::filesystem::remove(path);
    std::filesystem::remove(dir);
    TEST_PASS("test_pm_remove_only_active_clears_selection");
}

// ─── Test: Effective View (topological merge + TagResolver + cache) ────

TEST_CASE("test_pm_effective_view") {
    ProfileManager pm;
    pm.create("builtin:vanilla");
    auto& mod = pm.create("enchantencore");
    mod.set_dependencies({"builtin:vanilla"});
    mod.add_enchantment({NSID("mod:leeching"), "Leeching", MCE::All, 2, 2, 1, false, {}, {NSID("#minecraft:swords")}});
    mod.add_tag({NSID("#minecraft:swords"), "swords"});
    auto& pack = pm.create("mypack");
    pack.set_dependencies({"enchantencore"});
    // pack overrides leeching's max_level
    pack.add_enchantment({NSID("mod:leeching"), "Leeching", MCE::All, 3, 3, 1, false, {}, {NSID("#minecraft:swords")}});

    auto& eff = pm.resolve_effective("mypack");
    expect(eff.ench().contains(NSID("mod:leeching")), "effective view contains dep enchant");
    expect(eff.ench().at(NSID("mod:leeching")).max_level == 3, "pack overrides dep");
    expect(eff.tag_resolver() != nullptr, "effective view carries TagResolver");
    TEST_PASS("test_pm_effective_view");
}

// ─── Test: merge is insert_or_assign for ALL domains (upper overrides lower) ──
// 终审发现：RegistryHelper::merge 的 equipment/tags 分支原本是 add-if-absent
// （下层赢），与魔咒分支及 ProfileManager::merge 文档语义（"Source entries
// overwrite dest entries on conflict"）矛盾——datapack 对 vanilla 装备的字段
// 修改（如 max_durability=999）在 effective 视图里被静默丢弃。统一为
// insert_or_assign 后上层覆盖下层。

TEST_CASE("test_pm_merge_equipment_tag_upper_wins") {
    ProfileManager pm;
    auto& vanilla = pm.create("builtin:vanilla");
    vanilla.add_equipment(Equipment{NSID("minecraft:copper_boots"), "Copper Boots", NSID("#minecraft:boots"), 143});
    vanilla.add_tag({NSID("#minecraft:boots"), "boots"});

    // datapack 风格上层：修改 vanilla 装备字段 + 覆盖 vanilla tag 名 + 新增内容。
    auto& pack = pm.create("mod_eq");
    pack.set_dependencies({"builtin:vanilla"});
    pack.add_equipment(Equipment{NSID("minecraft:copper_boots"), "Copper Boots", NSID("#minecraft:boots"), 999});
    pack.add_equipment(Equipment{NSID("minecraft:mod_blade"), "Mod Blade", NSID("#minecraft:sword"), 777});
    pack.add_tag({NSID("#minecraft:boots"), "boots-overridden"});
    pack.add_tag({NSID("#minecraft:new_tag"), "new_tag"});

    const Profile& eff = pm.resolve_effective("mod_eq");
    // 装备：上层覆盖下层（原 add-if-absent 会保留 vanilla 的 143 —— bug）。
    expect_eq(eff.eq().at(NSID("minecraft:copper_boots")).max_durability, 999,
              "upper equipment field override wins in effective view");
    expect_eq(eff.eq().at(NSID("minecraft:mod_blade")).max_durability, 777,
              "new upper equipment present");
    // tag 名：上层覆盖下层。
    expect_eq(eff.tags().at(NSID("#minecraft:boots")).name, "boots-overridden",
              "upper tag name wins in effective view");
    expect(eff.tags().contains(NSID("#minecraft:new_tag")), "new upper tag present");

    // ProfileManager::merge（用户级 merge 操作）同样 insert_or_assign。
    pm.create("dest");
    pm.merge("mod_eq", "dest");
    expect_eq(pm.find("dest")->eq().at(NSID("minecraft:copper_boots")).max_durability, 999,
              "ProfileManager::merge overwrites dest equipment on conflict");
    expect_eq(pm.find("dest")->tags().at(NSID("#minecraft:boots")).name, "boots-overridden",
              "ProfileManager::merge overwrites dest tag on conflict");

    TEST_PASS("test_pm_merge_equipment_tag_upper_wins");
}

// ─── Test: JSON `name` is the profile key (load_directory + ProfileLoader agree) ──
// B-T26 #18: the same file must load under the SAME key via load_directory and
// ProfileLoader::load.  JSON top-level `name` wins when present and non-empty;
// otherwise the file stem is used.

TEST_CASE("test_pm_load_directory_json_name_key") {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / ("besq_pm_name_key_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);

    auto with_name = dir / "a.json";
    {
        std::ofstream f(with_name);
        f << R"({
            "name": "custom_key",
            "dependencies": ["builtin:vanilla"],
            "enchantments": [],
            "equipments": [],
            "categories": [],
            "tags": {}
        })";
    }
    auto no_name = dir / "b.json";
    {
        std::ofstream f(no_name);
        f << R"({
            "dependencies": ["builtin:vanilla"],
            "enchantments": [],
            "equipments": [],
            "categories": [],
            "tags": {}
        })";
    }

    // ProfileLoader::load — the file's own key.
    ProfileLoader loader;
    Profile a = loader.load(with_name);
    expect(a.name() == "custom_key", "ProfileLoader::load uses JSON name as key");
    Profile b = loader.load(no_name);
    expect(b.name() == "b", "ProfileLoader::load falls back to file stem without name");

    // load_directory — same keys.
    ProfileManager pm;
    pm.load_directory(dir);
    expect(pm.exists("custom_key"), "load_directory registers JSON-name key");
    expect(pm.exists("b"), "load_directory registers stem key without name");
    expect(!pm.exists("a"), "file a.json NOT registered under its stem (JSON name wins)");

    std::filesystem::remove(with_name);
    std::filesystem::remove(no_name);
    std::filesystem::remove(dir);
    TEST_PASS("test_pm_load_directory_json_name_key");
}

// ─── Test: ProfileLoader::load restores full metadata from the JSON root ──
// C4: description/author/version/mc_version/display_name/parent were dropped
// by the name-only ProfileMetadata constructor in the load path.  A
// native-JSON profile that declares them must see them restored after load
// (both the ProfileLoader::load file path and the load_directory manager path).

TEST_CASE("test_pm_loader_restores_metadata") {
    auto dir = std::filesystem::temp_directory_path() / "besq_pm_meta_restore";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto f = dir / "meta.json";
    {
        std::ofstream out(f);
        out << R"({
            "name": "meta_profile",
            "description": "Restored description",
            "author": "Tester",
            "version": "3.1.4",
            "mc_version": "1.21.4",
            "display_name": "Meta Profile",
            "parent": "base_profile",
            "dependencies": ["builtin:vanilla"],
            "enchantments": [],
            "equipments": [],
            "categories": [],
            "tags": {}
        })";
    }

    // ProfileLoader::load — the file path restores every declared field.
    ProfileLoader loader;
    Profile p = loader.load(f);
    expect(p.metadata().description == "Restored description", "load restores description");
    expect(p.metadata().author == "Tester", "load restores author");
    expect(p.metadata().version == "3.1.4", "load restores version");
    expect(p.metadata().mc_version == "1.21.4", "load restores mc_version");
    expect(p.metadata().display_name == "Meta Profile", "load restores display_name");
    expect(p.metadata().parent == "base_profile", "load restores parent");
    expect(p.dependencies().size() == 1 && p.dependencies()[0] == "builtin:vanilla", "load keeps declared dependencies");

    // load_directory — the manager path restores metadata identically.
    ProfileManager pm;
    pm.load_directory(dir);
    const Profile* q = pm.find("meta_profile");
    expect(q != nullptr, "load_directory registers the JSON-name key");
    expect(q != nullptr && q->metadata().author == "Tester", "load_directory restores author");
    expect(q != nullptr && q->metadata().mc_version == "1.21.4", "load_directory restores mc_version");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_loader_restores_metadata");
}

// ─── Test: builtin:vanilla metadata restored from vanilla.json root ───────
// C4: load_builtin used a name-only constructor so description/author/version/
// mc_version were all dropped.  After the fix, the builtin profile carries the
// vanilla.json top-level metadata — mc_version is the MC release id written by
// scripts/vanilla/enchantment.py::write_output from res/vanilla/version.json
// (currently 26.2).

TEST_CASE("test_pm_builtin_metadata") {
    ProfileLoader loader;
    Profile p = loader.load_builtin();
    expect(p.name() == "builtin:vanilla", "builtin key unchanged");
    expect(!p.metadata().description.empty(), "builtin description restored (non-empty)");
    expect(p.metadata().author == "BestEnchSeq", "builtin author restored");
    expect(p.metadata().version == "2.0.0", "builtin version restored");
    expect(p.metadata().mc_version == "26.2", "builtin mc_version = MC release id from res/vanilla/version.json");
    TEST_PASS("test_pm_builtin_metadata");
}

// ─── Test: tag merge direction — higher-priority source wins (B-T26 #19) ───
// build_tag_resolver must take member data from the LAST (highest-priority)
// source that defines a tag, matching the effective-view merge direction
// (upper overrides lower).

TEST_CASE("test_pm_tag_merge_direction") {
    ProfileManager pm;
    auto& a = pm.create("a");
    auto ra = std::make_shared<TagResolver>();
    ra->add_tag("minecraft:swords", {"minecraft:diamond_sword"});
    a.set_tag_resolver(ra);
    a.add_tag({NSID("#minecraft:swords"), "swords"});

    auto& b = pm.create("b");
    b.set_dependencies({"a"});
    auto rb = std::make_shared<TagResolver>();
    rb->add_tag("minecraft:swords", {"minecraft:netherite_sword"});
    b.set_tag_resolver(rb);
    b.add_tag({NSID("#minecraft:swords"), "swords"});

    const Profile& eff = pm.resolve_effective("b");
    const TagResolver* tr = eff.tag_resolver();
    expect(tr != nullptr, "effective view carries TagResolver");
    if (tr) {
        const auto* m = tr->get_tag("minecraft", "swords");
        expect(m != nullptr, "merged tag present in effective resolver");
        if (m) {
            expect(m->count("minecraft:netherite_sword") == 1, "higher-priority B's member wins");
            expect(m->count("minecraft:diamond_sword") == 0, "lower-priority A's member overridden");
        }
    }
    TEST_PASS("test_pm_tag_merge_direction");
}

// ─── Test: resolve_effective injects vanilla base (B-T26 #20) ─────────────
// A profile with NO declared dependencies still gets vanilla equipment (real
// max_durability, not a 0 placeholder) and a vanilla enchant in its effective
// view.

TEST_CASE("test_pm_effective_injects_vanilla") {
    ProfileManager pm;
    auto& vanilla = pm.create("builtin:vanilla");
    vanilla.add_equipment(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID("#minecraft:sword"), 1561});
    vanilla.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));
    pm.create("mypack"); // no declared dependencies

    const Profile& eff = pm.resolve_effective("mypack");
    expect(eff.has_equipment(NSID("minecraft:diamond_sword")), "effective view includes vanilla equipment");
    if (eff.has_equipment(NSID("minecraft:diamond_sword")))
        expect_eq(eff.eq().at(NSID("minecraft:diamond_sword")).max_durability, 1561,
                  "vanilla equipment has real max_durability (not a 0 placeholder)");
    expect(eff.has_enchantment(NSID("minecraft:sharpness")), "effective view includes vanilla enchant");
    TEST_PASS("test_pm_effective_injects_vanilla");
}

// ─── Profile group (composite) effective views ─────────────────────────

TEST_CASE("test_pm_group_effective_merge") {
    ProfileManager pm;
    auto& vanilla = pm.create("builtin:vanilla");
    vanilla.add_equipment(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID("#minecraft:sword"), 1561});
    auto& a = pm.create("pack_a");
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 7));
    auto& b = pm.create("pack_b");
    b.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 3));
    b.add_equipment(Equipment{NSID("minecraft:copper_boots"), "Copper Boots", NSID("#minecraft:boots"), 143});

    const Profile& eff = pm.resolve_effective_group({"pack_a", "pack_b"});
    expect(eff.has_enchantment(NSID("minecraft:sharpness")), "group view includes member enchant");
    expect_eq(eff.ench().at(NSID("minecraft:sharpness")).max_level, 3, "later member wins (pack_b over pack_a)");
    expect(eff.has_equipment(NSID("minecraft:copper_boots")), "group view includes pack_b equipment");
    expect(eff.has_equipment(NSID("minecraft:diamond_sword")), "group view includes implicit vanilla base");

    TEST_PASS("test_pm_group_effective_merge");
}

TEST_CASE("test_pm_group_dedup_last_wins") {
    ProfileManager pm;
    pm.create("builtin:vanilla");
    auto& a = pm.create("pack_a");
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 9));
    auto& b = pm.create("pack_b");
    b.set_dependencies({"pack_a"});
    b.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 4));

    // 组合 b,a：b 依赖 a → 展开 vanilla,a,b,a → 去重（保留最后出现）→ vanilla,b,a。
    const Profile& eff = pm.resolve_effective_group({"pack_b", "pack_a"});
    expect_eq(eff.ench().at(NSID("minecraft:sharpness")).max_level, 9,
              "user order wins: explicit pack_a listed last overrides its position in pack_b's chain");

    TEST_PASS("test_pm_group_dedup_last_wins");
}

TEST_CASE("test_pm_group_implicit_vanilla") {
    ProfileManager pm;
    auto& vanilla = pm.create("builtin:vanilla");
    vanilla.add_equipment(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID("#minecraft:sword"), 1561});
    // datapack 风格成员：无 dependencies（datapack 格式无法声明依赖链）。
    auto& pack = pm.create("enchantology_like");
    pack.add_enchantment(make_ench("mod:ember", "Ember", 3));

    const Profile& eff = pm.resolve_effective_group({"enchantology_like"});
    expect(eff.has_equipment(NSID("minecraft:diamond_sword")),
           "group member without declared deps still gets implicit vanilla base");
    expect(eff.has_enchantment(NSID("mod:ember")), "member content present");
    expect(eff.tag_resolver() != nullptr, "group view has a TagResolver");

    TEST_PASS("test_pm_group_implicit_vanilla");
}

TEST_CASE("test_pm_group_member_not_found") {
    ProfileManager pm;
    pm.create("builtin:vanilla");
    pm.create("pack_a");
    bool threw = false;
    try {
        pm.resolve_effective_group({"pack_a", "missing_pack"});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "group with an unknown member throws");

    TEST_PASS("test_pm_group_member_not_found");
}

TEST_CASE("test_pm_group_cycle_throws") {
    ProfileManager pm;
    auto& a = pm.create("pack_a");
    auto& b = pm.create("pack_b");
    a.set_dependencies({"pack_b"});
    b.set_dependencies({"pack_a"});
    bool threw = false;
    try {
        pm.resolve_effective_group({"pack_a"});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "group containing a cyclic member throws");

    TEST_PASS("test_pm_group_cycle_throws");
}

TEST_CASE("test_pm_group_cache_invalidate") {
    ProfileManager pm;
    pm.create("builtin:vanilla");
    auto& a = pm.create("pack_a");
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));
    auto& b = pm.create("pack_b");
    b.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 2));

    expect_eq(pm.resolve_effective_group({"pack_a", "pack_b"}).ench().at(NSID("minecraft:sharpness")).max_level, 2,
              "first resolve (pack_b wins)");

    // manager 级变更 → 有效视图缓存失效 → 组合视图反映新值。
    EnchInfo updated = b.ench().at(NSID("minecraft:sharpness"));
    updated.max_level = 8;
    expect(pm.update_enchantment("pack_b", updated), "update succeeds");
    expect_eq(pm.resolve_effective_group({"pack_a", "pack_b"}).ench().at(NSID("minecraft:sharpness")).max_level, 8,
              "group cache invalidated by manager mutation");

    TEST_PASS("test_pm_group_cache_invalidate");
}

TEST_CASE("test_pm_group_publish") {
    ProfileManager pm;
    auto& vanilla = pm.create("builtin:vanilla");
    vanilla.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));
    auto& a = pm.create("pack_a");
    a.add_enchantment(make_ench("mod:ember", "Ember", 3));
    auto& b = pm.create("pack_b");
    b.add_enchantment(make_ench("mod:flame", "Flame", 2));

    auto tmp = std::filesystem::temp_directory_path() / "besq_group_publish.json";
    std::filesystem::remove(tmp);
    expect(pm.publish("pack_a,pack_b", "1.0", "", tmp), "composite publish succeeds");
    expect(std::filesystem::exists(tmp), "publish file written");

    // 拍平的组合视图应包含两成员内容 + 隐式 vanilla。
    Profile loaded;
    {
        std::ifstream f(tmp);
        std::stringstream ss;
        ss << f.rdbuf();
        auto json = Json::parse(ss.str());
        json >> loaded;
    }
    expect(loaded.has_enchantment(NSID("mod:ember")), "published composite has pack_a enchant");
    expect(loaded.has_enchantment(NSID("mod:flame")), "published composite has pack_b enchant");
    expect(loaded.has_enchantment(NSID("minecraft:sharpness")), "published composite has implicit vanilla base");
    // 组合含未知成员 → 失败。
    expect(!pm.publish("pack_a,missing_pack", "1.0", "", tmp), "composite publish with unknown member fails");

    std::filesystem::remove(tmp);
    TEST_PASS("test_pm_group_publish");
}

// ─── Publish round-trip: the published file must reload via ProfileLoader ──
// 终审发现（既有 bug）：to_json 把 equipment category 序列化为完整 NSID
// ("#minecraft:bow")，而 ProfileLoader 两阶段路径在 from_dto 里无条件拼
// "#minecraft:" + category → 双重前缀非法。修复后产物可被 ProfileLoader
// 重新加载，category 语义不变（tag 形式）。

TEST_CASE("test_pm_publish_roundtrip_reload") {
    ProfileManager pm;
    auto& vanilla = pm.create("builtin:vanilla");
    vanilla.add_equipment(Equipment{NSID("minecraft:bow"), "Bow", NSID("#minecraft:bow"), 384});
    // mod 前缀魔咒（vanilla 宇宙没有）→ 断言保真时不会被内置数据兜底掩盖。
    vanilla.add_enchantment(EnchInfo{NSID("mod:power"), "Power", MCE::All, 7, 7, 2, false,
                                     {}, {NSID("#minecraft:swords")}});

    auto tmp = std::filesystem::temp_directory_path() / "besq_publish_roundtrip.json";
    std::filesystem::remove(tmp);
    expect(pm.publish("builtin:vanilla", "1.0", "", tmp), "single-profile publish succeeds");

    // 经 ProfileLoader（与 load_directory 相同的两阶段 RegistryLoader 路径）
    // 重新加载——修复前在此抛 '#minecraft:#minecraft:bow' is invalid。
    ProfileLoader loader;
    Profile reloaded = loader.load(tmp);
    expect(reloaded.has_equipment(NSID("minecraft:bow")), "reloaded publish has equipment");
    if (reloaded.has_equipment(NSID("minecraft:bow"))) {
        const auto& bow = reloaded.eq().at(NSID("minecraft:bow"));
        expect_eq(bow.max_durability, 384, "reloaded durability intact");
        expect(bow.category.is_tag() && bow.category.get_id() == "bow",
               "reloaded category keeps tag form #minecraft:bow");
    }
    expect(reloaded.has_enchantment(NSID("mod:power")), "reloaded publish has mod enchantment");
    if (reloaded.has_enchantment(NSID("mod:power"))) {
        const auto& power = reloaded.ench().at(NSID("mod:power"));
        expect_eq(power.max_level, 7, "reloaded enchantment content preserved (not vanilla fallback)");
        expect(power.supported_items.count(NSID("#minecraft:swords")) == 1,
               "reloaded supported_items preserved");
    }

    std::filesystem::remove(tmp);
    TEST_PASS("test_pm_publish_roundtrip_reload");
}

// ─── Test: Manager-level edit (real-time validation) + snapshot/undo ────

TEST_CASE("test_pm_edit_snapshot_undo") {
    ProfileManager pm;
    auto& p = pm.create("test:edit");
    p.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}});
    p.add_enchantment({NSID("minecraft:smite"),
                       "Smite",
                       MCE::All,
                       5,
                       5,
                       1,
                       false,
                       {NSID("minecraft:sharpness")},
                       {NSID("#minecraft:swords")}});

    // 实时校验：给 smite 加不存在的 exclusive 引用 → 拒绝（不应用、无快照）
    EnchInfo bad = p.ench().at(NSID("minecraft:smite"));
    bad.exclusive_set.insert(NSID("nonexistent:ench"));
    expect(!pm.update_enchantment("test:edit", bad), "invalid exclusive ref rejected");
    // 拒绝的变更未应用
    expect(p.ench().at(NSID("minecraft:smite")).exclusive_set.count(NSID("nonexistent:ench")) == 0,
           "rejected edit leaves profile untouched");

    // 合法编辑 → 应用；undo 回滚
    EnchInfo patch = p.ench().at(NSID("minecraft:sharpness"));
    patch.max_level = 6;
    expect(pm.update_enchantment("test:edit", patch), "valid edit applied");
    expect(p.ench().at(NSID("minecraft:sharpness")).max_level == 6, "max_level updated");
    expect(pm.undo("test:edit"), "undo succeeds");
    expect(p.ench().at(NSID("minecraft:sharpness")).max_level == 5, "undo reverts max_level");

    // 连续两次 undo：第二次应失败（仅回滚最近一次）
    expect(!pm.undo("test:edit"), "second undo fails (log exhausted)");

    TEST_PASS("test_pm_edit_snapshot_undo");
}

// ─── Test: rejected edit / undo preserve the attached TagResolver ──────

TEST_CASE("test_pm_edit_preserves_tag_resolver") {
    ProfileManager pm;
    auto& p = pm.create("test:resolver");
    p.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}});
    p.set_tag_resolver(std::make_shared<TagResolver>());
    expect(p.tag_resolver() != nullptr, "resolver attached before edits");

    // 被拒绝的编辑（max_level < 1 → 事后校验失败）：resolver 不应丢失
    EnchInfo bad = p.ench().at(NSID("minecraft:sharpness"));
    bad.max_level = 0;
    expect(!pm.update_enchantment("test:resolver", bad), "invalid edit rejected");
    expect(p.tag_resolver() != nullptr, "resolver survives rejected edit");

    // 合法编辑 + undo：resolver 不应丢失
    EnchInfo patch = p.ench().at(NSID("minecraft:sharpness"));
    patch.max_level = 6;
    expect(pm.update_enchantment("test:resolver", patch), "valid edit applied");
    expect(pm.undo("test:resolver"), "undo succeeds");
    expect(p.tag_resolver() != nullptr, "resolver survives undo");

    TEST_PASS("test_pm_edit_preserves_tag_resolver");
}

// ─── Test: Manager-level add/remove (enchantment/equipment/tag) + undo ──

TEST_CASE("test_pm_crud_full") {
    ProfileManager pm;
    auto& p = pm.create("test:crud");

    EnchInfo e1{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}};
    expect(pm.add_enchantment("test:crud", e1), "manager add_enchantment");
    expect(p.ench().contains(NSID("minecraft:sharpness")), "enchantment present after add");

    Equipment eq{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID("#minecraft:swords"), 1561};
    expect(pm.add_equipment("test:crud", eq), "manager add_equipment");
    expect(p.eq().contains(NSID("minecraft:diamond_sword")), "equipment present after add");

    expect(pm.add_tag("test:crud", EquipmentTag(NSID("#minecraft:crudgroup"), "Crud")), "manager add_tag");
    expect(p.tags().contains(NSID("#minecraft:crudgroup")), "tag present after add");

    expect(pm.remove_tag("test:crud", NSID("#minecraft:crudgroup")), "manager remove_tag");
    expect(!p.tags().contains(NSID("#minecraft:crudgroup")), "tag removed");

    expect(pm.remove_equipment("test:crud", NSID("minecraft:diamond_sword")), "manager remove_equipment");
    expect(!p.eq().contains(NSID("minecraft:diamond_sword")), "equipment removed");

    expect(pm.remove_enchantment("test:crud", NSID("minecraft:sharpness")), "manager remove_enchantment");
    expect(!p.ench().contains(NSID("minecraft:sharpness")), "enchantment removed");

    // undo rolls back the last change (remove_enchantment)
    expect(pm.undo("test:crud"), "undo succeeds");
    expect(p.ench().contains(NSID("minecraft:sharpness")), "undo restores sharpness");

    TEST_PASS("test_pm_crud_full");
}

// ─── Test: Versioned publish (flatten effective view + version/tag) ──────

TEST_CASE("test_pm_publish") {
    ProfileManager pm;
    pm.create("builtin:vanilla");
    auto& pack = pm.create("mypack");
    pack.set_dependencies({"builtin:vanilla"});
    pack.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}});

    auto tmp = std::filesystem::temp_directory_path() / "besq_publish_test.json";
    bool ok = pm.publish("mypack", "1.0.0", "stable", tmp);
    expect(ok, "publish succeeds");
    // 自包含：有效视图（依赖链合并）的 sharpness 存在 + version 内嵌
    auto json = Json::parse(file_utils::read_file(tmp));
    expect(json.has("enchantments"), "published file has enchantments");
    bool sharp = false;
    for (const auto& e : json["enchantments"].as_array())
        if (e.as<Json::Object>().at("id").as<std::string>() == "minecraft:sharpness")
            sharp = true;
    expect(sharp, "published file contains merged dep enchantment");
    expect(json.has("version") && json["version"].as<std::string>() == "1.0.0", "version embedded");
    expect(json.has("release_tag") && json["release_tag"].as<std::string>() == "stable", "tag embedded");
    std::filesystem::remove(tmp);
    TEST_PASS("test_pm_publish");
}

// ─── Test: Load Datapack (pack.mcmeta detection + load_datapack) ─────────

TEST_CASE("test_pm_load_datapack") {
    // Build a minimal datapack inline in a temp dir (NO res/ fixtures —
    // everything must be committed or runtime-built).
    // B-T14 M-4: profile key prefers the FOLDER STEM verbatim.  Use a folder
    // name with spaces + a dot so the verbatim behavior is observable.
    auto dir = std::filesystem::temp_directory_path() / "More Enchants 1.4";
    std::filesystem::remove_all(dir); // stale cleanup from prior runs
    std::filesystem::create_directories(dir / "data" / "mytest" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "minecraft" / "tags" / "item");

    // pack.mcmeta — `pack.id` is typically a UUID; B-T13 profile keys are plain
    // std::string (verbatim, no NSID sanitization), B-T14 M-4 prefers the folder
    // stem and ignores pack.id here.
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({
            "pack": {
                "description": "test pack",
                "pack_format": 15,
                "id": "8a3c7f5b-0000-4b1a-9d7e-abc123def456"
            }
        })";
    }
    // One enchantment referencing the minecraft:swords item tag.
    {
        std::ofstream f(dir / "data" / "mytest" / "enchantment" / "leeching.json");
        f << R"({
            "description": "Leeching",
            "supported_items": "#minecraft:swords",
            "anvil_cost": 2,
            "max_level": 3,
            "min_cost": {"base": 5, "per_level_above_first": 5},
            "max_cost": {"base": 20, "per_level_above_first": 5}
        })";
    }
    // Item tag so the enchantment's supported_items reference resolves.
    {
        std::ofstream f(dir / "data" / "minecraft" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }

    ProfileManager pm;
    bool ok = pm.load_datapack(dir);
    expect(ok, "load_datapack returns true for a valid datapack");
    expect(pm.exists("More Enchants 1.4"), "profile name derived from FOLDER STEM verbatim (spaces + dot)");
    expect(pm.exists("builtin:vanilla"), "builtin:vanilla root injected");

    const Profile* dp = pm.find("More Enchants 1.4");
    expect(dp != nullptr, "datapack profile findable");
    if (dp) {
        // Content ids stay NSIDs — only the profile key became a plain string.
        expect(dp->has_enchantment(NSID("mytest:leeching")), "leeching loaded into profile");
        const auto& supp = dp->ench().at(NSID("mytest:leeching")).supported_items;
        expect(supp.count(NSID("#minecraft:swords")) == 1, "leeching keeps #minecraft:swords after cross_validate");
        expect(dp->tag_resolver() != nullptr, "datapack profile carries vanilla∪datapack TagResolver");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack");
}

// ─── Test: datapack limited_level computed by LimitedLevelCalculator (B-T18) ──
// The datapack parser no longer computes limited_level inline; after load the
// calculator must have back-filled it from min_cost.  leeching: max_level=3,
// min_cost {base:5, per_level_above_first:5}, supported #minecraft:swords →
// diamond_sword (enchantability 10): power = round((31+4)*1.15) = 40,
// level = (40-5)/5+1 = 8 → clamped to max_level 3 → 3.

TEST_CASE("test_pm_load_datapack_computes_limited_level") {
    auto dir = std::filesystem::temp_directory_path() / "LL Datapack";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "mytest" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "minecraft" / "tags" / "item");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    {
        std::ofstream f(dir / "data" / "mytest" / "enchantment" / "leeching.json");
        f << R"({
            "description": "Leeching",
            "supported_items": "#minecraft:swords",
            "anvil_cost": 2,
            "max_level": 3,
            "min_cost": {"base": 5, "per_level_above_first": 5}
        })";
    }
    {
        std::ofstream f(dir / "data" / "minecraft" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }

    ProfileManager pm;
    bool ok = pm.load_datapack(dir);
    expect(ok, "load_datapack succeeds");
    const Profile* dp = pm.find("LL Datapack");
    expect(dp != nullptr, "datapack profile findable");
    if (dp) {
        expect(dp->has_enchantment(NSID("mytest:leeching")), "leeching loaded");
        const auto& leech = dp->ench().at(NSID("mytest:leeching"));
        expect(leech.min_cost_base == 5, "min_cost.base carried into EnchInfo");
        expect(leech.min_cost_per_level == 5, "min_cost.per_level carried into EnchInfo");
        expect_eq(leech.limited_level, 3, "limited_level computed by LimitedLevelCalculator (not lost by parser removal)");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_computes_limited_level");
}

// ─── Test: datapack treasure derivation via the treasure tag (B-T19) ────
// MC datapack enchantment definitions carry no is_treasure field; the parser
// derives it from `#minecraft:enchantment/treasure` (vanilla fallback) ∪ the
// datapack's own treasure-tag override.  A treasure member gets limited_level 0
// from the calculator; a non-member keeps its computed level.

TEST_CASE("test_pm_load_datapack_treasure_tag") {
    auto dir = std::filesystem::temp_directory_path() / "Treasure Datapack";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "mytest" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "minecraft" / "tags" / "item");
    std::filesystem::create_directories(dir / "data" / "minecraft" / "tags" / "enchantment");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    {
        // Custom enchantment added to the treasure tag override below.
        std::ofstream f(dir / "data" / "mytest" / "enchantment" / "wind_glide.json");
        f << R"({
            "description": "Wind Glide",
            "supported_items": "#minecraft:swords",
            "anvil_cost": 3,
            "max_level": 2,
            "min_cost": {"base": 10, "per_level_above_first": 6}
        })";
    }
    {
        // Non-treasure control: no treasure-tag membership.
        std::ofstream f(dir / "data" / "mytest" / "enchantment" / "plain_power.json");
        f << R"({
            "description": "Plain Power",
            "supported_items": "#minecraft:swords",
            "anvil_cost": 2,
            "max_level": 4,
            "min_cost": {"base": 1, "per_level_above_first": 11}
        })";
    }
    {
        std::ofstream f(dir / "data" / "minecraft" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }
    {
        // Treasure-tag override (category-dropped key `minecraft:treasure`
        // under parse_files derivation) adds the custom member.
        std::ofstream f(dir / "data" / "minecraft" / "tags" / "enchantment" / "treasure.json");
        f << R"({"values": ["mytest:wind_glide"]})";
    }

    ProfileManager pm;
    bool ok = pm.load_datapack(dir);
    expect(ok, "load_datapack succeeds");
    const Profile* dp = pm.find("Treasure Datapack");
    expect(dp != nullptr, "datapack profile findable");
    if (dp) {
        expect(dp->has_enchantment(NSID("mytest:wind_glide")), "wind_glide loaded");
        const auto& glide = dp->ench().at(NSID("mytest:wind_glide"));
        expect(glide.is_treasure, "wind_glide: is_treasure derived from datapack treasure tag");
        expect_eq(glide.limited_level, 0, "treasure member → limited_level 0");

        expect(dp->has_enchantment(NSID("mytest:plain_power")), "plain_power loaded");
        const auto& plain = dp->ench().at(NSID("mytest:plain_power"));
        expect(!plain.is_treasure, "plain_power: not a treasure member");
        expect(plain.limited_level > 0, "non-treasure → computed limited_level > 0");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_treasure_tag");
}

// ─── Test: Datapack-defined item tags survive load (B-T14 I-1) ───────────

TEST_CASE("test_pm_load_datapack_custom_tag") {
    // A datapack that defines its OWN item tag (#mypack:magic_staffs) and an
    // enchantment referencing it.  Previously the enchantment was dropped
    // entirely at from_dto because the tag wasn't in the vanilla universe.
    auto dir = std::filesystem::temp_directory_path() / "Magic Staff Pack";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "mypack" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "mypack" / "tags" / "item");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    // Brand-new item tag defined by the datapack itself.
    {
        std::ofstream f(dir / "data" / "mypack" / "tags" / "item" / "magic_staffs.json");
        f << R"({"values": ["mypack:magic_staff"]})";
    }
    // Enchantment whose supported_items references the datapack's own tag.
    {
        std::ofstream f(dir / "data" / "mypack" / "enchantment" / "staff_power.json");
        f << R"({
            "description": "Staff Power",
            "supported_items": "#mypack:magic_staffs",
            "anvil_cost": 3,
            "max_level": 4,
            "min_cost": {"base": 8, "per_level_above_first": 6}
        })";
    }

    ProfileManager pm;
    bool ok = pm.load_datapack(dir);
    expect(ok, "load_datapack succeeds");
    const Profile* dp = pm.find("Magic Staff Pack");
    expect(dp != nullptr, "datapack profile findable");
    if (dp) {
        expect(dp->has_enchantment(NSID("mypack:staff_power")), "staff_power survives load (datapack tag in profile universe)");
        const auto& supp = dp->ench().at(NSID("mypack:staff_power")).supported_items;
        expect(supp.count(NSID("#mypack:magic_staffs")) == 1, "staff_power keeps #mypack:magic_staffs after cross_validate");
        expect(dp->tags().contains(NSID("#mypack:magic_staffs")), "datapack item tag present in profile tag universe");

        // tags_of applicability at solve time — direct resolver + effective view.
        const TagResolver* tr = dp->tag_resolver();
        expect(tr != nullptr, "resolver attached");
        if (tr)
            expect(tr->tags_of("mypack:magic_staff").count(NSID("#mypack:magic_staffs")) == 1,
                   "tags_of(mypack:magic_staff) includes #mypack:magic_staffs");

        const Profile& eff = pm.resolve_effective("Magic Staff Pack");
        const TagResolver* etr = eff.tag_resolver();
        expect(etr != nullptr, "effective view carries TagResolver");
        if (etr)
            expect(etr->tags_of("mypack:magic_staff").count(NSID("#mypack:magic_staffs")) == 1,
                   "effective tags_of honors datapack item tag");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_custom_tag");
}

// ─── Test: Vanilla-tag override honored (replace + merge) (B-T14 I-1) ─────

TEST_CASE("test_pm_load_datapack_vanilla_tag_override") {
    // A datapack that REPLACES #minecraft:swords with a custom sword item.
    // The override must drive tags_of at solve time (direct + effective view).
    auto dir = std::filesystem::temp_directory_path() / "Vanilla Swords Override";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "minecraft" / "tags" / "item");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    {
        std::ofstream f(dir / "data" / "minecraft" / "tags" / "item" / "swords.json");
        f << R"({"replace": true, "values": ["mypack:great_sword"]})";
    }

    ProfileManager pm;
    bool ok = pm.load_datapack(dir);
    expect(ok, "load_datapack succeeds");
    const Profile* dp = pm.find("Vanilla Swords Override");
    expect(dp != nullptr, "datapack profile findable");
    if (dp) {
        const TagResolver* tr = dp->tag_resolver();
        expect(tr != nullptr, "resolver attached");
        if (tr) {
            expect(tr->tags_of("mypack:great_sword").count(NSID("#minecraft:swords")) == 1,
                   "tags_of(great_sword) includes #minecraft:swords (override)");
            expect(tr->tags_of("minecraft:diamond_sword").count(NSID("#minecraft:swords")) == 0,
                   "tags_of(diamond_sword) excludes #minecraft:swords (replace=true)");
        }

        // Effective view honors the override too.
        const Profile& eff = pm.resolve_effective("Vanilla Swords Override");
        const TagResolver* etr = eff.tag_resolver();
        expect(etr != nullptr, "effective view carries TagResolver");
        if (etr)
            expect(etr->tags_of("mypack:great_sword").count(NSID("#minecraft:swords")) == 1,
                   "effective tags_of honors vanilla-tag override");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_vanilla_tag_override");
}

// ─── Test: invalid tag key is skipped, not fatal (B-T14 follow-up) ───────

TEST_CASE("test_pm_load_datapack_skips_invalid_tag_key") {
    // A tag file whose path contains a space and uppercase → tag id
    // "mypack:My Tag" is invalid (NSID rejects spaces and uppercase).  It must
    // be skipped, NOT abort the whole datapack load; valid tags are still
    // processed.
    auto dir = std::filesystem::temp_directory_path() / "Valid And Odd Tags";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "mypack" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "mypack" / "tags" / "item");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    // Valid item tag.
    {
        std::ofstream f(dir / "data" / "mypack" / "tags" / "item" / "magic_staffs.json");
        f << R"({"values": ["mypack:magic_staff"]})";
    }
    // Invalid-named tag file (space in the id) — must be skipped.
    {
        std::ofstream f(dir / "data" / "mypack" / "tags" / "item" / "My Tag.json");
        f << R"({"values": ["mypack:whatever"]})";
    }
    // Enchantment referencing the valid tag.
    {
        std::ofstream f(dir / "data" / "mypack" / "enchantment" / "staff_power.json");
        f << R"({
            "description": "Staff Power",
            "supported_items": "#mypack:magic_staffs",
            "anvil_cost": 3,
            "max_level": 4,
            "min_cost": {"base": 8, "per_level_above_first": 6}
        })";
    }

    ProfileManager pm;
    bool ok = pm.load_datapack(dir);
    expect(ok, "load_datapack succeeds despite an invalid tag key");
    const Profile* dp = pm.find("Valid And Odd Tags");
    expect(dp != nullptr, "datapack profile findable");
    if (dp) {
        expect(dp->has_enchantment(NSID("mypack:staff_power")), "staff_power survives load");
        expect(dp->tags().contains(NSID("#mypack:magic_staffs")), "valid item tag present");
        const TagResolver* tr = dp->tag_resolver();
        expect(tr != nullptr, "resolver attached");
        if (tr)
            expect(tr->tags_of("mypack:magic_staff").count(NSID("#mypack:magic_staffs")) == 1, "valid tag drives tags_of");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_skips_invalid_tag_key");
}

// ─── Test: direct Profile::set_dependencies invalidates effective view (M-1) ──

TEST_CASE("test_pm_direct_set_dependencies_invalidates_effective") {
    ProfileManager pm;
    pm.create("builtin:vanilla");
    auto& base = pm.create("base");
    base.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));
    auto& pack = pm.create("mypack");

    // Populate the effective-view cache: mypack with no deps → no sharpness.
    const Profile& eff0 = pm.resolve_effective("mypack");
    expect(!eff0.ench().contains(NSID("minecraft:sharpness")), "before dep, effective view has no sharpness");

    // Bypass the manager: mutate dependencies directly on the Profile.
    pack.set_dependencies({"base"});

    // The next resolve_effective must honor the new dep (cache invalidated).
    const Profile& eff1 = pm.resolve_effective("mypack");
    expect(eff1.ench().contains(NSID("minecraft:sharpness")),
           "after direct set_dependencies, effective view sees dep enchantment");

    TEST_PASS("test_pm_direct_set_dependencies_invalidates_effective");
}

// ─── Test: Load Directory detects datapack subdirectories ───────────────

TEST_CASE("test_pm_load_directory_with_datapack") {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / ("besq_pm_dir_dp_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);

    // Native JSON profile in the root.
    auto native = dir / "bare_mod.json";
    {
        std::ofstream f(native);
        f << R"({
            "name": "bare_mod",
            "dependencies": ["builtin:vanilla"],
            "enchantments": [],
            "equipments": [],
            "categories": [],
            "tags": {}
        })";
    }

    // Datapack SUBDIRECTORY (no pack.id → directory-stem name, VERBATIM).
    auto dp = dir / "My Pack";
    std::filesystem::create_directories(dp / "data" / "mydp" / "enchantment");
    std::filesystem::create_directories(dp / "data" / "minecraft" / "tags" / "item");
    {
        std::ofstream f(dp / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    {
        std::ofstream f(dp / "data" / "mydp" / "enchantment" / "moonwalk.json");
        f << R"({
            "description": "Moonwalk",
            "supported_items": "#minecraft:swords",
            "anvil_cost": 4,
            "max_level": 3,
            "min_cost": {"base": 15, "per_level_above_first": 9}
        })";
    }
    {
        std::ofstream f(dp / "data" / "minecraft" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }

    ProfileManager pm;
    pm.load_directory(dir);

    expect(pm.exists("bare_mod"), "native profile loaded from file");
    expect(pm.exists("My Pack"), "datapack subdirectory loaded as profile (directory stem verbatim)");
    expect(pm.exists("builtin:vanilla"), "builtin:vanilla base auto-created");

    const Profile* dp_p = pm.find("My Pack");
    expect(dp_p != nullptr, "datapack profile findable");
    if (dp_p)
        expect(dp_p->has_enchantment(NSID("mydp:moonwalk")), "datapack enchantment loaded");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_directory_with_datapack");
}

// ─── Test: load_datapack on a dir WITHOUT pack.mcmeta ───────────────────

TEST_CASE("test_pm_load_datapack_no_mcmeta") {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / ("besq_pm_nomcmeta_" + std::to_string(++counter));
    // Datapack-like data dir, but NO pack.mcmeta → not a datapack.
    std::filesystem::create_directories(dir / "data" / "x" / "enchantment");

    ProfileManager pm;
    bool ok = pm.load_datapack(dir);
    expect(!ok, "load_datapack returns false when pack.mcmeta is absent");
    expect(pm.size() == 0, "no profiles registered on failure");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_no_mcmeta");
}

// ─── Test: datapack whose name would collide with the root key ──────────

TEST_CASE("test_pm_load_datapack_vanilla_name") {
    // A datapack whose FOLDER STEM is "vanilla" (legacy alias of the injected
    // root key) must be disambiguated so it never replaces the base profile.
    auto dir = std::filesystem::temp_directory_path() / "vanilla";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "vdp" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "minecraft" / "tags" / "item");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    {
        std::ofstream f(dir / "data" / "vdp" / "enchantment" / "leeching.json");
        f << R"({
            "description": "Leeching",
            "supported_items": "#minecraft:swords",
            "anvil_cost": 2,
            "max_level": 3,
            "min_cost": {"base": 5, "per_level_above_first": 5}
        })";
    }
    {
        std::ofstream f(dir / "data" / "minecraft" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }

    ProfileManager pm;
    bool ok = pm.load_datapack(dir);
    expect(ok, "load_datapack succeeds for a pack named vanilla");
    expect(pm.exists("builtin:vanilla"), "builtin:vanilla base profile preserved");
    expect(pm.exists("vanilla_datapack"), "datapack name disambiguated to vanilla_datapack");
    const Profile* v = pm.find("builtin:vanilla");
    expect(v != nullptr && !v->has_enchantment(NSID("vdp:leeching")), "builtin:vanilla base not replaced by datapack content");
    const Profile* dp = pm.find("vanilla_datapack");
    expect(dp != nullptr && dp->has_enchantment(NSID("vdp:leeching")),
           "datapack enchantment lives under the disambiguated name");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_vanilla_name");
}

// ─── Test: datapack whose name equals the current root key (builtin:vanilla) ──

TEST_CASE("test_pm_load_datapack_builtin_vanilla_name") {
    // B-T14 M-4: `pack.id` is a FALLBACK only — even a pack.id of
    // "builtin:vanilla" must not rename the profile away from the folder stem.
    auto dir = std::filesystem::temp_directory_path() / "My Vanilla Replacer";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "vdp" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "minecraft" / "tags" / "item");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15, "id": "builtin:vanilla"}})";
    }
    {
        std::ofstream f(dir / "data" / "vdp" / "enchantment" / "leeching.json");
        f << R"({
            "description": "Leeching",
            "supported_items": "#minecraft:swords",
            "anvil_cost": 2,
            "max_level": 3,
            "min_cost": {"base": 5, "per_level_above_first": 5}
        })";
    }
    {
        std::ofstream f(dir / "data" / "minecraft" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }

    ProfileManager pm;
    // Pre-seed the injected root with content so "not replaced" is observable.
    auto& root = pm.create("builtin:vanilla");
    root.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}});

    bool ok = pm.load_datapack(dir);
    expect(ok, "load_datapack succeeds");
    expect(pm.exists("builtin:vanilla"), "builtin:vanilla base profile preserved");
    expect(pm.exists("My Vanilla Replacer"), "profile key = folder stem, NOT pack.id (M-4)");
    const Profile* v = pm.find("builtin:vanilla");
    expect(v != nullptr && v->has_enchantment(NSID("minecraft:sharpness")),
           "builtin:vanilla base content intact (not replaced by datapack)");
    expect(v != nullptr && !v->has_enchantment(NSID("vdp:leeching")),
           "builtin:vanilla base does not contain datapack enchantment");
    const Profile* dp = pm.find("My Vanilla Replacer");
    expect(dp != nullptr && dp->has_enchantment(NSID("vdp:leeching")), "datapack enchantment lives under the folder-stem name");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_builtin_vanilla_name");
}

// ─── Test: datapack name derivation (verbatim, B-T13) ───────────────────

TEST_CASE("test_pm_name_derive") {
    // B-T13: profile keys are plain std::string.  B-T14 M-4: derive_datapack_name
    // prefers the FOLDER STEM verbatim — spaces/dots are preserved, no NSID
    // charset sanitization; pack.id is used ONLY when the folder has no stem.
    auto root = std::filesystem::temp_directory_path();

    // Folder stem with spaces + a dot wins over a UUID pack.id.
    auto stem_dir = root / "More Enchants 1.4";
    std::filesystem::remove_all(stem_dir);
    std::filesystem::create_directories(stem_dir);
    {
        std::ofstream f(stem_dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15, "id": "8a3c7f5b-0000-4b1a-9d7e-abc123def456"}})";
    }
    expect_eq(derive_datapack_name(stem_dir), std::string("More Enchants 1.4"),
              "folder stem verbatim (spaces + dot) over pack.id");

    // A folder literally named "vanilla" is disambiguated (legacy alias guard).
    auto vanilla_dir = root / "vanilla";
    std::filesystem::remove_all(vanilla_dir);
    std::filesystem::create_directories(vanilla_dir);
    {
        std::ofstream f(vanilla_dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15, "id": "8a3c7f5b-0000-4b1a-9d7e-abc123def456"}})";
    }
    expect_eq(derive_datapack_name(vanilla_dir), std::string("vanilla_datapack"), "folder stem 'vanilla' is disambiguated");

    // No pack.mcmeta at all → directory stem verbatim.
    auto bare_dir = root / "bare_stem";
    std::filesystem::remove_all(bare_dir);
    std::filesystem::create_directories(bare_dir);
    expect_eq(derive_datapack_name(bare_dir), std::string("bare_stem"), "no pack.mcmeta → directory stem verbatim");

    // Malformed pack.mcmeta → directory stem verbatim.
    auto malformed_dir = root / "malformed_stem";
    std::filesystem::remove_all(malformed_dir);
    std::filesystem::create_directories(malformed_dir);
    {
        std::ofstream f(malformed_dir / "pack.mcmeta");
        f << "{not valid json";
    }
    expect_eq(derive_datapack_name(malformed_dir), std::string("malformed_stem"),
              "malformed pack.mcmeta → directory stem verbatim");

    // A directory with no stem falls back to a non-empty name.
    expect_eq(derive_datapack_name(std::filesystem::current_path().root_path()), std::string("datapack"),
              "directory with no stem → 'datapack' fallback");

    std::filesystem::remove_all(stem_dir);
    std::filesystem::remove_all(vanilla_dir);
    std::filesystem::remove_all(bare_dir);
    std::filesystem::remove_all(malformed_dir);
    TEST_PASS("test_pm_name_derive");
}

// ─── Test: empty profile keys are rejected at manager entry points ──────

TEST_CASE("test_pm_empty_key_rejected") {
    ProfileManager pm;
    pm.create("base");

    expect_throws_as<std::invalid_argument>([&]() { pm.create(""); }, "create with empty name throws");
    expect_throws_as<std::invalid_argument>([&]() { pm.create_from("base", ""); }, "create_from with empty dest throws");
    expect_throws_as<std::invalid_argument>([&]() { pm.snapshot("base", ""); }, "snapshot with empty name throws");
    expect_throws_as<std::invalid_argument>([&]() { pm.branch("base", ""); }, "branch with empty name throws");

    TEST_PASS("test_pm_empty_key_rejected");
}

// ─── Test: FormatDetector::detect pack.mcmeta branch ────────────────────

// ─── Test: ProfileLoader::load on a datapack dir keeps #mypack:* tags (#24) ──
// load_into shares the two-phase RegistryLoader path; after the fix the parsed
// datapack item_tags seed the validation universe AND land in the profile's tag
// registry so a `#mypack:*`-referencing enchantment survives (previously the
// FormatDetector dropped item_tags, so the enchantment was silently removed).

TEST_CASE("test_profile_loader_load_datapack_keeps_tags") {
    auto dir = std::filesystem::temp_directory_path() / "Loader Custom Pack";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data" / "mypack" / "enchantment");
    std::filesystem::create_directories(dir / "data" / "mypack" / "tags" / "item");
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    {
        std::ofstream f(dir / "data" / "mypack" / "tags" / "item" / "swords.json");
        f << R"({"values": ["minecraft:diamond_sword"]})";
    }
    {
        std::ofstream f(dir / "data" / "mypack" / "enchantment" / "leeching.json");
        f << R"({
            "description": "Leeching",
            "supported_items": "#mypack:swords",
            "anvil_cost": 2,
            "max_level": 3,
            "min_cost": {"base": 5, "per_level_above_first": 5}
        })";
    }

    ProfileLoader loader;
    Profile p = loader.load(dir);
    expect(p.has_enchantment(NSID("mypack:leeching")), "leeching with #mypack:* supported_items survives ProfileLoader::load");
    const auto& supp = p.ench().at(NSID("mypack:leeching")).supported_items;
    expect(supp.count(NSID("#mypack:swords")) == 1, "leeching keeps #mypack:swords after load");
    expect(p.tags().contains(NSID("#mypack:swords")), "datapack item tag #mypack:swords lands in the profile's tag registry");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_profile_loader_load_datapack_keeps_tags");
}

TEST_CASE("test_format_detector_datapack") {
    static int counter = 0;

    // Directory with pack.mcmeta → McOfficial (new primary check).
    auto dir = std::filesystem::temp_directory_path() / ("besq_fmt_dp_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    expect(FormatDetector::detect(dir) == DataFormat::McOfficial, "dir with pack.mcmeta detected as McOfficial");

    // Directory WITHOUT pack.mcmeta but with data/<ns>/enchantment → still
    // McOfficial via the secondary data/ scan.
    auto alt = std::filesystem::temp_directory_path() / ("besq_fmt_dp2_" + std::to_string(counter));
    std::filesystem::create_directories(alt / "data" / "ns" / "enchantment");
    expect(FormatDetector::detect(alt) == DataFormat::McOfficial,
           "dir with data/<ns>/enchantment still detected as McOfficial");

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(alt);
    TEST_PASS("test_format_detector_datapack");
}

// ─── Test: load_directory skips equipments_*.csv companion files ────────
// T6: companion equipment files (equipments_<stem>.csv) load only through
// their main CSV file; they must NOT become standalone profiles.  The
// companion equipment still round-trips into the main profile via
// FormatDetector's NativeCsv branch.

TEST_CASE("test_load_directory_skips_equipments_csv") {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / ("besq_prof_dir_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);

    std::ofstream(dir / "pack.csv") << "id,name,max_level,multiplier,exclusive_set,supported_items\n"
                                       "mod:sharp,Sharp,5,1,,\"#minecraft:swords\"\n";
    std::ofstream(dir / "equipments_pack.csv") << "id,name,category,max_durability\n"
                                                  "minecraft:diamond_sword,Diamond Sword,sword,1561\n";

    ProfileManager pm;
    pm.load_directory(dir);
    expect(pm.exists("pack"), "pack profile loaded");
    expect(!pm.exists("equipments_pack"), "equipments_* companion NOT a standalone profile");
    // The enchantment survives cross-validation (#minecraft:swords is a real
    // vanilla tag) and the companion equipment flows into the main profile
    // (Step 1 read-back).
    const Profile* pack = pm.find("pack");
    expect(pack != nullptr, "pack profile findable");
    if (pack) {
        expect_eq(static_cast<int>(pack->ench().size()), 1, "pack profile carries the enchantment from the main CSV");
        expect_eq(static_cast<int>(pack->eq().size()), 1, "pack profile carries companion equipment");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_load_directory_skips_equipments_csv");
}

// ─── Test: update_equipment (round-trip via _mutate) ────────────────────
// Manager-level equipment update: patch by NSID id; missing id → false.

TEST_CASE("test_update_equipment") {
    ProfileManager pm;
    pm.create("p1");
    Equipment eq;
    eq.id = NSID("diamond_sword");
    eq.max_durability = 1561;
    expect(pm.add_equipment("p1", eq), "add eq");
    Equipment patch = eq;
    patch.max_durability = 999;
    expect(pm.update_equipment("p1", patch), "update eq");
    Profile* p = pm.find("p1");
    expect(p != nullptr, "p1 exists");
    if (p)
        expect(p->eq().find(NSID("diamond_sword"))->max_durability == 999, "updated value");
    // missing id → false (no change)
    Equipment missing = patch;
    missing.id = NSID("netherite_sword");
    expect(!pm.update_equipment("p1", missing), "update missing → false");
    TEST_PASS("test_update_equipment");
}

// ─── Test: update_tag + set_dependencies (round-trip via _mutate) ───────

TEST_CASE("test_update_tag_and_deps") {
    ProfileManager pm;
    pm.create("p1");
    pm.create("p2");
    expect(pm.set_dependencies("p1", {"p2"}), "set deps");
    auto deps = pm.resolve_dependencies("p1");
    expect(deps.size() == 1 && deps[0] == "p2", "deps applied");
    EquipmentTag tag;
    tag.id = NSID("minecraft:swords");
    expect(pm.add_tag("p1", tag), "add tag");
    tag.name = "blades";
    expect(pm.update_tag("p1", tag), "update tag");
    expect(pm.find("p1")->tags().find(NSID("minecraft:swords"))->name == "blades", "tag updated");
    // unknown profile → false
    expect(!pm.set_dependencies("nope", {"p2"}), "set deps unknown profile → false");
    // cycle → false, dependencies unchanged
    ProfileManager pm2;
    pm2.create("a");
    pm2.create("b");
    expect(pm2.set_dependencies("a", {"b"}), "a→b ok");
    expect(!pm2.set_dependencies("b", {"a"}), "b→a would cycle → rejected");
    expect(pm2.find("b")->dependencies().empty(), "b deps unchanged after rejected set");
    TEST_PASS("test_update_tag_and_deps");
}

// ─── Test: rename (map key reorder; active-name sync) ───────────────────

TEST_CASE("test_rename") {
    ProfileManager pm;
    pm.create("old");
    expect(pm.rename("old", "new"), "rename");
    expect(!pm.exists("old") && pm.exists("new"), "renamed");
    expect(!pm.rename("old", "new"), "rename missing → false");
    // target already exists → false
    pm.create("taken");
    expect(!pm.rename("new", "taken"), "rename onto existing → false");
    // active-name follows the rename
    ProfileManager pm2;
    pm2.create("act");
    pm2.activate("act");
    expect(pm2.rename("act", "act2"), "rename active");
    expect(pm2.active_name() == "act2", "active name follows rename");
    TEST_PASS("test_rename");
}

// ─── Test: Profile::clone 深拷贝 TagResolver（create_from/snapshot/branch
//        派生独立回归门）──────────────────────────────────────────────────

TEST_CASE("test_pm_clone_resolver_independence") {
    ProfileManager pm;
    auto& src = pm.create("test:src");
    src.add_tag(EquipmentTag(NSID("#test:group"), "Group"));
    src.add_tag(EquipmentTag(NSID("#test:weapons"), "Weapons"));
    auto res = std::make_shared<TagResolver>();
    res->add_tag("test:group", {"minecraft:sword", "minecraft:axe"});
    res->add_tag("test:weapons", {"minecraft:sword"});
    src.set_tag_resolver(res);

    // 三种派生路径全部受益于 clone 深拷贝。
    auto& cf = pm.create_from("test:src", "test:cf");
    auto& snap = pm.snapshot("test:src", "test:snap");
    auto& br = pm.branch("test:src", "test:branch");

    // 派生初始拥有源 resolver 的副本（值一致、对象独立）。
    for (const Profile* d : {&cf, &snap, &br}) {
        expect(d->tag_resolver() != nullptr, "derived has resolver");
        const auto* raw = d->tag_resolver()->raw_values("test:group");
        expect(raw != nullptr && raw->size() == 2, "derived resolver seeded from source");
    }

    // 改派生 resolver → 源 raw_values 不变（深拷贝回归门）。
    auto cf_res = cf.tag_resolver_ptr();
    cf_res->add_tag("test:group", {"minecraft:bow"});
    const auto* src_raw = src.tag_resolver()->raw_values("test:group");
    expect(src_raw != nullptr && src_raw->size() == 2, "source raw values size unchanged");
    bool has_sword = false, has_axe = false, has_bow = false;
    for (const auto& v : *src_raw)
        if (const auto* e = std::get_if<EntryRef>(&v)) {
            has_sword = has_sword || e->id == "minecraft:sword";
            has_axe = has_axe || e->id == "minecraft:axe";
            has_bow = has_bow || e->id == "minecraft:bow";
        }
    expect(has_sword && has_axe && !has_bow, "source resolver untouched by derived mutation");
    const auto* cf_raw = cf.tag_resolver()->raw_values("test:group");
    expect(cf_raw != nullptr && cf_raw->size() == 1, "derived resolver updated");

    // 反向：源改 → 派生不变。
    src.tag_resolver_ptr()->add_tag("test:group", {"minecraft:shovel"});
    const auto* cf_raw2 = cf.tag_resolver()->raw_values("test:group");
    expect(cf_raw2 != nullptr && cf_raw2->size() == 1, "derived unaffected by source mutation");

    // snapshot/branch 同样独立。
    auto snap_res = snap.tag_resolver_ptr();
    snap_res->add_tag("test:weapons", {"minecraft:bow"});
    const auto* src_w = src.tag_resolver()->raw_values("test:weapons");
    expect(src_w != nullptr && src_w->size() == 1, "snapshot mutation does not leak to source");
    auto br_res = br.tag_resolver_ptr();
    br_res->add_tag("test:weapons", {"minecraft:bow"});
    const auto* snap_w = snap.tag_resolver()->raw_values("test:weapons");
    expect(snap_w != nullptr && snap_w->size() == 1, "branch mutation does not leak to snapshot");

    TEST_PASS("clone resolver independence");
}

// ─── Main ───────────────────────────────────────────────────────────────
