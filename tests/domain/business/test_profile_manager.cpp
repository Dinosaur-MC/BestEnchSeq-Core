#include "framework/test_utils.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "common/io/json.h"
#include "common/io/FileUtils.hpp"

#include <filesystem>
#include <fstream>
#include <string>

// ─── Helper: create an enchantment info for testing ─────────────────────

static EnchInfo make_ench(const std::string& id_str, const std::string& name, int max_level) {
    return EnchInfo{NSID(id_str), name, MCE::All, max_level, max_level, 1, false,
                    std::unordered_set<NSID>{}, std::unordered_set<NSID>{}};
}

// ─── Test: Create Profile ───────────────────────────────────────────────

void test_create_profile() {
    ProfileManager mgr;
    auto& p = mgr.create("test:profile_a");

    expect(mgr.exists("test:profile_a"), "profile should exist after create");
    expect(mgr.size() == 1, "manager size should be 1 after one create");

    // Verify profile can be modified via returned reference
    bool added = p.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));
    expect(added, "add_enchantment should succeed");
    expect(p.has_enchantment(NSID("minecraft:sharpness")), "profile should contain sharpness");

    std::cout << "PASS: test_create_profile" << std::endl;
}

// ─── Test: Create From ──────────────────────────────────────────────────

void test_create_from() {
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

    std::cout << "PASS: test_create_from" << std::endl;
}

// ─── Test: Remove Profile ───────────────────────────────────────────────

void test_remove_profile() {
    ProfileManager mgr;
    mgr.create("test:a");
    mgr.create("test:b");

    bool removed = mgr.remove("test:a");
    expect(removed, "remove should return true for existing profile");
    expect(mgr.size() == 1, "size should be 1 after removing one profile");
    expect(!mgr.exists("test:a"), "removed profile should not exist");
    expect(mgr.exists("test:b"), "remaining profile should still exist");

    std::cout << "PASS: test_remove_profile" << std::endl;
}

// ─── Test: Remove Nonexistent ───────────────────────────────────────────

void test_remove_nonexistent() {
    ProfileManager mgr;
    mgr.create("test:a");

    bool removed = mgr.remove("test:nonexistent");
    expect(!removed, "remove should return false for nonexistent profile");
    expect(mgr.size() == 1, "size should remain unchanged");

    std::cout << "PASS: test_remove_nonexistent" << std::endl;
}

// ─── Test: Activate and Active ──────────────────────────────────────────

void test_activate_and_active() {
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

    std::cout << "PASS: test_activate_and_active" << std::endl;
}

// ─── Test: Activate Nonexistent Throws ──────────────────────────────────

void test_activate_nonexistent_throws() {
    ProfileManager mgr;
    mgr.create("test:a");

    expect_throws_as<std::runtime_error>([&]() {
        mgr.activate("test:nonexistent");
    }, "activate() should throw for nonexistent profile");

    std::cout << "PASS: test_activate_nonexistent_throws" << std::endl;
}

// ─── Test: Empty Active Throws ──────────────────────────────────────────

void test_empty_active_throws() {
    ProfileManager mgr;  // no profiles

    expect_throws_as<std::runtime_error>([&]() {
        mgr.active();
    }, "active() should throw when manager is empty");

    std::cout << "PASS: test_empty_active_throws" << std::endl;
}

// ─── Test: List ─────────────────────────────────────────────────────────

void test_list() {
    ProfileManager mgr;
    mgr.create("test:alpha");
    mgr.create("test:beta");
    mgr.create("test:gamma");

    auto names = mgr.list();
    expect(names.size() == 3, "list should return 3 names");

    // Each of the expected names must be in the list
    bool found_alpha = false, found_beta = false, found_gamma = false;
    for (const auto& n : names) {
        if (n == "test:alpha")   found_alpha = true;
        if (n == "test:beta")    found_beta  = true;
        if (n == "test:gamma")   found_gamma = true;
    }
    expect(found_alpha, "list should contain alpha");
    expect(found_beta,  "list should contain beta");
    expect(found_gamma, "list should contain gamma");

    std::cout << "PASS: test_list" << std::endl;
}

// ─── Test: Find ─────────────────────────────────────────────────────────

void test_find() {
    ProfileManager mgr;
    mgr.create("test:found_me");

    Profile* p = mgr.find("test:found_me");
    expect(p != nullptr, "find() should return non-null for existing profile");

    Profile* q = mgr.find("test:unknown");
    expect(q == nullptr, "find() should return null for unknown profile");

    std::cout << "PASS: test_find" << std::endl;
}

// ─── Test: Snapshot ─────────────────────────────────────────────────────

void test_snapshot() {
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

    std::cout << "PASS: test_snapshot" << std::endl;
}

// ─── Test: Branch ───────────────────────────────────────────────────────

void test_branch() {
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

    std::cout << "PASS: test_branch" << std::endl;
}

// ─── Test: Merge ────────────────────────────────────────────────────────

void test_merge() {
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

    std::cout << "PASS: test_merge" << std::endl;
}

// ─── Test: Merge with missing source/dest throws (I-2 regression) ────────

void test_pm_merge_missing_throws() {
    ProfileManager pm;
    pm.create("base");

    expect_throws_as<std::runtime_error>([&]() { pm.merge("missing", "base"); },
        "merge with missing source throws");
    expect_throws_as<std::runtime_error>([&]() { pm.merge("base", "missing"); },
        "merge with missing dest throws");

    TEST_PASS("test_pm_merge_missing_throws");
}

// ─── Test: Create Empty Profile Structure ───────────────────────────────

void test_create_empty_profile_structure() {
    ProfileManager mgr;
    auto& p = mgr.create("test:empty");

    // Check default metadata version
    expect(p.metadata().version.empty(), "default version should be empty string");
    expect(p.metadata().name == "test:empty", "name should match create parameter");
    expect(p.ench().size() == 0, "empty profile should have 0 enchantments");
    expect(p.eq().size() == 0, "empty profile should have 0 equipment");
    expect(p.tags().size() == 0, "empty profile should have 0 tags");

    std::cout << "PASS: test_create_empty_profile_structure" << std::endl;
}

// ─── Test: Dependency Chain (transitive topological resolution) ──────

void test_pm_dependency_chain() {
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

void test_pm_dependency_cycle() {
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

void test_pm_dependency_cycle_throws() {
    ProfileManager pm;
    auto& a = pm.create("a");
    auto& b = pm.create("b");
    a.set_dependencies({"b"});
    b.set_dependencies({"a"});

    expect(pm.is_cyclic("a"), "a is cyclic");
    expect(pm.is_cyclic("b"), "b is cyclic");
    expect(!pm.is_cyclic("nonexistent"), "nonexistent profile is NOT cyclic");
    expect_throws_as<std::runtime_error>([&]() { pm.resolve_effective("a"); },
        "resolve_effective on a cyclic profile throws");

    TEST_PASS("test_pm_dependency_cycle_throws");
}

// ─── Test: Cross-validate supported_items against dependency universe ─

void test_pm_cross_validate() {
    ProfileManager pm;
    auto& vanilla = pm.create("builtin:vanilla");
    vanilla.add_equipment(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword",
                                    NSID("#minecraft:sword"), 1561});
    vanilla.add_tag(EquipmentTag{NSID("#minecraft:sword"), "sword"});

    auto& mod = pm.create("mod");
    mod.set_dependencies({"builtin:vanilla"});

    // Valid refs (tag + concrete item from vanilla) plus one unknown item.
    EnchInfo sharp = make_ench("minecraft:sharpness", "Sharpness", 5);
    sharp.supported_items = {NSID("#minecraft:sword"), NSID("minecraft:diamond_sword"),
                             NSID("minecraft:stone")};
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

void test_pm_load_directory() {
    // Nonexistent directory is a safe no-op.
    ProfileManager pm;
    pm.load_directory(std::filesystem::temp_directory_path() / "besq_no_such_dir_xyz");
    expect(pm.size() == 0, "no profiles loaded from missing directory");

    // Temp directory with one native-JSON profile.
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() /
               ("besq_pm_dir_" + std::to_string(++counter));
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
        expect(deps.size() == 1 && deps[0] == "builtin:vanilla",
               "dependencies parsed from JSON root");
    }

    // Cleanup temp files.
    std::filesystem::remove(path);
    std::filesystem::remove(dir);
    TEST_PASS("test_pm_load_directory");
}

// ─── Test: Effective View (topological merge + TagResolver + cache) ────

void test_pm_effective_view() {
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

// ─── Test: JSON `name` is the profile key (load_directory + ProfileLoader agree) ──
// B-T26 #18: the same file must load under the SAME key via load_directory and
// ProfileLoader::load.  JSON top-level `name` wins when present and non-empty;
// otherwise the file stem is used.

void test_pm_load_directory_json_name_key() {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() /
               ("besq_pm_name_key_" + std::to_string(++counter));
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

// ─── Test: tag merge direction — higher-priority source wins (B-T26 #19) ───
// build_tag_resolver must take member data from the LAST (highest-priority)
// source that defines a tag, matching the effective-view merge direction
// (upper overrides lower).

void test_pm_tag_merge_direction() {
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
            expect(m->count("minecraft:netherite_sword") == 1,
                   "higher-priority B's member wins");
            expect(m->count("minecraft:diamond_sword") == 0,
                   "lower-priority A's member overridden");
        }
    }
    TEST_PASS("test_pm_tag_merge_direction");
}

// ─── Test: resolve_effective injects vanilla base (B-T26 #20) ─────────────
// A profile with NO declared dependencies still gets vanilla equipment (real
// max_durability, not a 0 placeholder) and a vanilla enchant in its effective
// view.

void test_pm_effective_injects_vanilla() {
    ProfileManager pm;
    auto& vanilla = pm.create("builtin:vanilla");
    vanilla.add_equipment(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword",
                                    NSID("#minecraft:sword"), 1561});
    vanilla.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));
    pm.create("mypack");   // no declared dependencies

    const Profile& eff = pm.resolve_effective("mypack");
    expect(eff.has_equipment(NSID("minecraft:diamond_sword")),
           "effective view includes vanilla equipment");
    if (eff.has_equipment(NSID("minecraft:diamond_sword")))
        expect_eq(eff.eq().at(NSID("minecraft:diamond_sword")).max_durability, 1561,
                  "vanilla equipment has real max_durability (not a 0 placeholder)");
    expect(eff.has_enchantment(NSID("minecraft:sharpness")),
           "effective view includes vanilla enchant");
    TEST_PASS("test_pm_effective_injects_vanilla");
}

// ─── Test: Manager-level edit (real-time validation) + snapshot/undo ────

void test_pm_edit_snapshot_undo() {
    ProfileManager pm;
    auto& p = pm.create("test:edit");
    p.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}});
    p.add_enchantment({NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false, {NSID("minecraft:sharpness")}, {NSID("#minecraft:swords")}});

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

void test_pm_edit_preserves_tag_resolver() {
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

// ─── Test: Versioned publish (flatten effective view + version/tag) ──────

void test_pm_publish() {
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
        if (e.as<Json::Object>().at("id").as<std::string>() == "minecraft:sharpness") sharp = true;
    expect(sharp, "published file contains merged dep enchantment");
    expect(json.has("version") && json["version"].as<std::string>() == "1.0.0", "version embedded");
    expect(json.has("release_tag") && json["release_tag"].as<std::string>() == "stable", "tag embedded");
    std::filesystem::remove(tmp);
    TEST_PASS("test_pm_publish");
}

// ─── Test: Load Datapack (pack.mcmeta detection + load_datapack) ─────────

void test_pm_load_datapack() {
    // Build a minimal datapack inline in a temp dir (NO res/ fixtures —
    // everything must be committed or runtime-built).
    // B-T14 M-4: profile key prefers the FOLDER STEM verbatim.  Use a folder
    // name with spaces + a dot so the verbatim behavior is observable.
    auto dir = std::filesystem::temp_directory_path() / "More Enchants 1.4";
    std::filesystem::remove_all(dir);  // stale cleanup from prior runs
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
    expect(pm.exists("More Enchants 1.4"),
           "profile name derived from FOLDER STEM verbatim (spaces + dot)");
    expect(pm.exists("builtin:vanilla"), "builtin:vanilla root injected");

    const Profile* dp = pm.find("More Enchants 1.4");
    expect(dp != nullptr, "datapack profile findable");
    if (dp) {
        // Content ids stay NSIDs — only the profile key became a plain string.
        expect(dp->has_enchantment(NSID("mytest:leeching")), "leeching loaded into profile");
        const auto& supp = dp->ench().at(NSID("mytest:leeching")).supported_items;
        expect(supp.count(NSID("#minecraft:swords")) == 1,
               "leeching keeps #minecraft:swords after cross_validate");
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

void test_pm_load_datapack_computes_limited_level() {
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
        expect_eq(leech.limited_level, 3,
                  "limited_level computed by LimitedLevelCalculator (not lost by parser removal)");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_computes_limited_level");
}

// ─── Test: datapack treasure derivation via the treasure tag (B-T19) ────
// MC datapack enchantment definitions carry no is_treasure field; the parser
// derives it from `#minecraft:enchantment/treasure` (vanilla fallback) ∪ the
// datapack's own treasure-tag override.  A treasure member gets limited_level 0
// from the calculator; a non-member keeps its computed level.

void test_pm_load_datapack_treasure_tag() {
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
        expect(glide.is_treasure,
               "wind_glide: is_treasure derived from datapack treasure tag");
        expect_eq(glide.limited_level, 0,
                  "treasure member → limited_level 0");

        expect(dp->has_enchantment(NSID("mytest:plain_power")), "plain_power loaded");
        const auto& plain = dp->ench().at(NSID("mytest:plain_power"));
        expect(!plain.is_treasure, "plain_power: not a treasure member");
        expect(plain.limited_level > 0,
               "non-treasure → computed limited_level > 0");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_treasure_tag");
}

// ─── Test: Datapack-defined item tags survive load (B-T14 I-1) ───────────

void test_pm_load_datapack_custom_tag() {
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
        expect(dp->has_enchantment(NSID("mypack:staff_power")),
               "staff_power survives load (datapack tag in profile universe)");
        const auto& supp = dp->ench().at(NSID("mypack:staff_power")).supported_items;
        expect(supp.count(NSID("#mypack:magic_staffs")) == 1,
               "staff_power keeps #mypack:magic_staffs after cross_validate");
        expect(dp->tags().contains(NSID("#mypack:magic_staffs")),
               "datapack item tag present in profile tag universe");

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

void test_pm_load_datapack_vanilla_tag_override() {
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

void test_pm_load_datapack_skips_invalid_tag_key() {
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
        expect(dp->has_enchantment(NSID("mypack:staff_power")),
               "staff_power survives load");
        expect(dp->tags().contains(NSID("#mypack:magic_staffs")),
               "valid item tag present");
        const TagResolver* tr = dp->tag_resolver();
        expect(tr != nullptr, "resolver attached");
        if (tr)
            expect(tr->tags_of("mypack:magic_staff").count(NSID("#mypack:magic_staffs")) == 1,
                   "valid tag drives tags_of");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_skips_invalid_tag_key");
}

// ─── Test: direct Profile::set_dependencies invalidates effective view (M-1) ──

void test_pm_direct_set_dependencies_invalidates_effective() {
    ProfileManager pm;
    pm.create("builtin:vanilla");
    auto& base = pm.create("base");
    base.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));
    auto& pack = pm.create("mypack");

    // Populate the effective-view cache: mypack with no deps → no sharpness.
    const Profile& eff0 = pm.resolve_effective("mypack");
    expect(!eff0.ench().contains(NSID("minecraft:sharpness")),
           "before dep, effective view has no sharpness");

    // Bypass the manager: mutate dependencies directly on the Profile.
    pack.set_dependencies({"base"});

    // The next resolve_effective must honor the new dep (cache invalidated).
    const Profile& eff1 = pm.resolve_effective("mypack");
    expect(eff1.ench().contains(NSID("minecraft:sharpness")),
           "after direct set_dependencies, effective view sees dep enchantment");

    TEST_PASS("test_pm_direct_set_dependencies_invalidates_effective");
}

// ─── Test: Load Directory detects datapack subdirectories ───────────────

void test_pm_load_directory_with_datapack() {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() /
               ("besq_pm_dir_dp_" + std::to_string(++counter));
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
    expect(pm.exists("My Pack"),
           "datapack subdirectory loaded as profile (directory stem verbatim)");
    expect(pm.exists("builtin:vanilla"), "builtin:vanilla base auto-created");

    const Profile* dp_p = pm.find("My Pack");
    expect(dp_p != nullptr, "datapack profile findable");
    if (dp_p)
        expect(dp_p->has_enchantment(NSID("mydp:moonwalk")), "datapack enchantment loaded");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_directory_with_datapack");
}

// ─── Test: load_datapack on a dir WITHOUT pack.mcmeta ───────────────────

void test_pm_load_datapack_no_mcmeta() {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() /
               ("besq_pm_nomcmeta_" + std::to_string(++counter));
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

void test_pm_load_datapack_vanilla_name() {
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
    expect(pm.exists("vanilla_datapack"),
           "datapack name disambiguated to vanilla_datapack");
    const Profile* v = pm.find("builtin:vanilla");
    expect(v != nullptr && !v->has_enchantment(NSID("vdp:leeching")),
           "builtin:vanilla base not replaced by datapack content");
    const Profile* dp = pm.find("vanilla_datapack");
    expect(dp != nullptr && dp->has_enchantment(NSID("vdp:leeching")),
           "datapack enchantment lives under the disambiguated name");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_vanilla_name");
}

// ─── Test: datapack whose name equals the current root key (builtin:vanilla) ──

void test_pm_load_datapack_builtin_vanilla_name() {
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
    root.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5,
                          1, false, {}, {NSID("#minecraft:swords")}});

    bool ok = pm.load_datapack(dir);
    expect(ok, "load_datapack succeeds");
    expect(pm.exists("builtin:vanilla"), "builtin:vanilla base profile preserved");
    expect(pm.exists("My Vanilla Replacer"),
           "profile key = folder stem, NOT pack.id (M-4)");
    const Profile* v = pm.find("builtin:vanilla");
    expect(v != nullptr && v->has_enchantment(NSID("minecraft:sharpness")),
           "builtin:vanilla base content intact (not replaced by datapack)");
    expect(v != nullptr && !v->has_enchantment(NSID("vdp:leeching")),
           "builtin:vanilla base does not contain datapack enchantment");
    const Profile* dp = pm.find("My Vanilla Replacer");
    expect(dp != nullptr && dp->has_enchantment(NSID("vdp:leeching")),
           "datapack enchantment lives under the folder-stem name");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_pm_load_datapack_builtin_vanilla_name");
}

// ─── Test: datapack name derivation (verbatim, B-T13) ───────────────────

void test_pm_name_derive() {
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
    expect_eq(derive_datapack_name(vanilla_dir), std::string("vanilla_datapack"),
              "folder stem 'vanilla' is disambiguated");

    // No pack.mcmeta at all → directory stem verbatim.
    auto bare_dir = root / "bare_stem";
    std::filesystem::remove_all(bare_dir);
    std::filesystem::create_directories(bare_dir);
    expect_eq(derive_datapack_name(bare_dir), std::string("bare_stem"),
              "no pack.mcmeta → directory stem verbatim");

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
    expect_eq(derive_datapack_name(std::filesystem::current_path().root_path()),
              std::string("datapack"),
              "directory with no stem → 'datapack' fallback");

    std::filesystem::remove_all(stem_dir);
    std::filesystem::remove_all(vanilla_dir);
    std::filesystem::remove_all(bare_dir);
    std::filesystem::remove_all(malformed_dir);
    TEST_PASS("test_pm_name_derive");
}

// ─── Test: empty profile keys are rejected at manager entry points ──────

void test_pm_empty_key_rejected() {
    ProfileManager pm;
    pm.create("base");

    expect_throws_as<std::invalid_argument>([&]() { pm.create(""); },
        "create with empty name throws");
    expect_throws_as<std::invalid_argument>([&]() { pm.create_from("base", ""); },
        "create_from with empty dest throws");
    expect_throws_as<std::invalid_argument>([&]() { pm.snapshot("base", ""); },
        "snapshot with empty name throws");
    expect_throws_as<std::invalid_argument>([&]() { pm.branch("base", ""); },
        "branch with empty name throws");

    TEST_PASS("test_pm_empty_key_rejected");
}

// ─── Test: FormatDetector::detect pack.mcmeta branch ────────────────────

// ─── Test: ProfileLoader::load on a datapack dir keeps #mypack:* tags (#24) ──
// load_into shares the two-phase RegistryLoader path; after the fix the parsed
// datapack item_tags seed the validation universe AND land in the profile's tag
// registry so a `#mypack:*`-referencing enchantment survives (previously the
// FormatDetector dropped item_tags, so the enchantment was silently removed).

void test_profile_loader_load_datapack_keeps_tags() {
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
    expect(p.has_enchantment(NSID("mypack:leeching")),
           "leeching with #mypack:* supported_items survives ProfileLoader::load");
    const auto& supp = p.ench().at(NSID("mypack:leeching")).supported_items;
    expect(supp.count(NSID("#mypack:swords")) == 1,
           "leeching keeps #mypack:swords after load");
    expect(p.tags().contains(NSID("#mypack:swords")),
           "datapack item tag #mypack:swords lands in the profile's tag registry");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_profile_loader_load_datapack_keeps_tags");
}

void test_format_detector_datapack() {
    static int counter = 0;

    // Directory with pack.mcmeta → McOfficial (new primary check).
    auto dir = std::filesystem::temp_directory_path() /
               ("besq_fmt_dp_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);
    {
        std::ofstream f(dir / "pack.mcmeta");
        f << R"({"pack": {"pack_format": 15}})";
    }
    expect(FormatDetector::detect(dir) == DataFormat::McOfficial,
           "dir with pack.mcmeta detected as McOfficial");

    // Directory WITHOUT pack.mcmeta but with data/<ns>/enchantment → still
    // McOfficial via the secondary data/ scan.
    auto alt = std::filesystem::temp_directory_path() /
               ("besq_fmt_dp2_" + std::to_string(counter));
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

void test_load_directory_skips_equipments_csv() {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() /
               ("besq_prof_dir_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);

    std::ofstream(dir / "pack.csv") <<
        "id,name,max_level,multiplier,exclusive_set,supported_items\n"
        "mod:sharp,Sharp,5,1,,\"#minecraft:swords\"\n";
    std::ofstream(dir / "equipments_pack.csv") <<
        "id,name,category,max_durability\n"
        "minecraft:diamond_sword,Diamond Sword,sword,1561\n";

    ProfileManager pm;
    pm.load_directory(dir);
    expect(pm.exists("pack"), "pack profile loaded");
    expect(!pm.exists("equipments_pack"),
           "equipments_* companion NOT a standalone profile");
    // The enchantment survives cross-validation (#minecraft:swords is a real
    // vanilla tag) and the companion equipment flows into the main profile
    // (Step 1 read-back).
    const Profile* pack = pm.find("pack");
    expect(pack != nullptr, "pack profile findable");
    if (pack) {
        expect_eq(static_cast<int>(pack->ench().size()), 1,
                  "pack profile carries the enchantment from the main CSV");
        expect_eq(static_cast<int>(pack->eq().size()), 1,
                  "pack profile carries companion equipment");
    }

    std::filesystem::remove_all(dir);
    TEST_PASS("test_load_directory_skips_equipments_csv");
}

// ─── Main ───────────────────────────────────────────────────────────────

int main() {
    try {
        test_create_profile();
        test_create_from();
        test_remove_profile();
        test_remove_nonexistent();
        test_activate_and_active();
        test_activate_nonexistent_throws();
        test_empty_active_throws();
        test_list();
        test_find();
        test_snapshot();
        test_branch();
        test_merge();
        test_pm_merge_missing_throws();
        test_create_empty_profile_structure();
        test_pm_dependency_chain();
        test_pm_dependency_cycle();
        test_pm_dependency_cycle_throws();
        test_pm_cross_validate();
        test_pm_load_directory();
        test_pm_load_directory_json_name_key();
        test_pm_effective_view();
        test_pm_tag_merge_direction();
        test_pm_effective_injects_vanilla();
        test_pm_edit_snapshot_undo();
        test_pm_edit_preserves_tag_resolver();
        test_pm_publish();
        test_pm_load_datapack();
        test_pm_load_datapack_computes_limited_level();
        test_pm_load_datapack_treasure_tag();
        test_pm_load_datapack_custom_tag();
        test_pm_load_datapack_vanilla_tag_override();
        test_pm_load_datapack_skips_invalid_tag_key();
        test_pm_direct_set_dependencies_invalidates_effective();
        test_pm_load_directory_with_datapack();
        test_pm_load_datapack_no_mcmeta();
        test_pm_load_datapack_vanilla_name();
        test_pm_load_datapack_builtin_vanilla_name();
        test_pm_name_derive();
        test_pm_empty_key_rejected();
        test_profile_loader_load_datapack_keeps_tags();
        test_format_detector_datapack();
        test_load_directory_skips_equipments_csv();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
