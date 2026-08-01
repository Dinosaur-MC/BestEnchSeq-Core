#include "framework/test_utils.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/registries/EnchantmentRegistry.h"

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
    auto& p = mgr.create(NSID("test:profile_a"));

    expect(mgr.exists(NSID("test:profile_a")), "profile should exist after create");
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
    auto& a = mgr.create(NSID("test:a"));
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));

    // Create B from A
    auto& b = mgr.create_from(NSID("test:a"), NSID("test:b"));

    // Verify B exists and has same enchantments as A
    expect(mgr.exists(NSID("test:b")), "B should exist after create_from");
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
    mgr.create(NSID("test:a"));
    mgr.create(NSID("test:b"));

    bool removed = mgr.remove(NSID("test:a"));
    expect(removed, "remove should return true for existing profile");
    expect(mgr.size() == 1, "size should be 1 after removing one profile");
    expect(!mgr.exists(NSID("test:a")), "removed profile should not exist");
    expect(mgr.exists(NSID("test:b")), "remaining profile should still exist");

    std::cout << "PASS: test_remove_profile" << std::endl;
}

// ─── Test: Remove Nonexistent ───────────────────────────────────────────

void test_remove_nonexistent() {
    ProfileManager mgr;
    mgr.create(NSID("test:a"));

    bool removed = mgr.remove(NSID("test:nonexistent"));
    expect(!removed, "remove should return false for nonexistent profile");
    expect(mgr.size() == 1, "size should remain unchanged");

    std::cout << "PASS: test_remove_nonexistent" << std::endl;
}

// ─── Test: Activate and Active ──────────────────────────────────────────

void test_activate_and_active() {
    ProfileManager mgr;
    auto& a = mgr.create(NSID("test:a"));
    mgr.create(NSID("test:b"));

    // Activate first profile
    mgr.activate(NSID("test:a"));
    expect(mgr.active_name() == NSID("test:a"), "active name should be a");
    // active() returns the same profile reference
    expect(&mgr.active() == &a, "active() should return reference to profile a");

    // Activate second profile
    mgr.activate(NSID("test:b"));
    expect(mgr.active_name() == NSID("test:b"), "active name should be b after switching");

    std::cout << "PASS: test_activate_and_active" << std::endl;
}

// ─── Test: Activate Nonexistent Throws ──────────────────────────────────

void test_activate_nonexistent_throws() {
    ProfileManager mgr;
    mgr.create(NSID("test:a"));

    expect_throws_as<std::runtime_error>([&]() {
        mgr.activate(NSID("test:nonexistent"));
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
    mgr.create(NSID("test:alpha"));
    mgr.create(NSID("test:beta"));
    mgr.create(NSID("test:gamma"));

    auto names = mgr.list();
    expect(names.size() == 3, "list should return 3 names");

    // Each of the expected names must be in the list
    bool found_alpha = false, found_beta = false, found_gamma = false;
    for (const auto& n : names) {
        if (n == NSID("test:alpha"))   found_alpha = true;
        if (n == NSID("test:beta"))    found_beta  = true;
        if (n == NSID("test:gamma"))   found_gamma = true;
    }
    expect(found_alpha, "list should contain alpha");
    expect(found_beta,  "list should contain beta");
    expect(found_gamma, "list should contain gamma");

    std::cout << "PASS: test_list" << std::endl;
}

// ─── Test: Find ─────────────────────────────────────────────────────────

void test_find() {
    ProfileManager mgr;
    mgr.create(NSID("test:found_me"));

    Profile* p = mgr.find(NSID("test:found_me"));
    expect(p != nullptr, "find() should return non-null for existing profile");

    Profile* q = mgr.find(NSID("test:unknown"));
    expect(q == nullptr, "find() should return null for unknown profile");

    std::cout << "PASS: test_find" << std::endl;
}

// ─── Test: Snapshot ─────────────────────────────────────────────────────

void test_snapshot() {
    ProfileManager mgr;

    // Create profile A with an enchantment
    auto& a = mgr.create(NSID("test:a"));
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));

    // Snapshot
    auto& snap = mgr.snapshot(NSID("test:a"), NSID("test:a_snap"));

    // Verify snapshot exists and has same data
    expect(mgr.exists(NSID("test:a_snap")), "snapshot should exist");
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
    auto& a = mgr.create(NSID("test:a"));
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));

    // Branch
    auto& branch = mgr.branch(NSID("test:a"), NSID("test:a_branch"));

    // Verify branch exists and inherited data
    expect(mgr.exists(NSID("test:a_branch")), "branch should exist");
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
    auto& a = mgr.create(NSID("test:a"));
    a.add_enchantment(make_ench("minecraft:sharpness", "Sharpness", 5));

    // Create profile B with enchantment Y (unbreaking)
    auto& b = mgr.create(NSID("test:b"));
    b.add_enchantment(make_ench("minecraft:unbreaking", "Unbreaking", 3));

    // Merge B into A
    mgr.merge(NSID("test:b"), NSID("test:a"));

    // Verify A now has both X and Y
    expect(a.has_enchantment(NSID("minecraft:sharpness")), "A should have sharpness after merge");
    expect(a.has_enchantment(NSID("minecraft:unbreaking")), "A should have unbreaking after merge");
    expect(a.ench().size() == 2, "A should have 2 enchantments after merge");

    // B unchanged
    expect(b.has_enchantment(NSID("minecraft:unbreaking")), "B should still have unbreaking");
    expect(b.ench().size() == 1, "B should still have 1 enchantment");

    std::cout << "PASS: test_merge" << std::endl;
}

// ─── Test: Create Empty Profile Structure ───────────────────────────────

void test_create_empty_profile_structure() {
    ProfileManager mgr;
    auto& p = mgr.create(NSID("test:empty"));

    // Check default metadata version
    expect(p.metadata().version.empty(), "default version should be empty string");
    expect(p.metadata().name == NSID("test:empty"), "name should match create parameter");
    expect(p.ench().size() == 0, "empty profile should have 0 enchantments");
    expect(p.eq().size() == 0, "empty profile should have 0 equipment");
    expect(p.tags().size() == 0, "empty profile should have 0 tags");

    std::cout << "PASS: test_create_empty_profile_structure" << std::endl;
}

// ─── Test: Dependency Chain (transitive topological resolution) ──────

void test_pm_dependency_chain() {
    ProfileManager pm;
    pm.create(NSID("vanilla"));
    auto& mod = pm.create(NSID("enchantencore"));
    mod.set_dependencies({NSID("vanilla")});
    auto& pack = pm.create(NSID("mypack"));
    pack.set_dependencies({NSID("enchantencore")});

    auto chain = pm.resolve_dependencies(NSID("mypack"));
    // transitive: mypack -> enchantencore -> vanilla; deps before self, self excluded
    expect(chain.size() == 2, "mypack has 2 deps (enchantencore + vanilla)");
    expect(chain[0] == NSID("vanilla"), "vanilla first (leaf dep)");
    expect(chain[1] == NSID("enchantencore"), "enchantencore second");

    TEST_PASS("test_pm_dependency_chain");
}

// ─── Test: Dependency Cycle Detection ────────────────────────────────

void test_pm_dependency_cycle() {
    ProfileManager pm;
    auto& a = pm.create(NSID("a"));
    auto& b = pm.create(NSID("b"));
    a.set_dependencies({NSID("b")});
    b.set_dependencies({NSID("a")});

    auto chain = pm.resolve_dependencies(NSID("a"));
    expect(chain.empty(), "cycle detected → empty chain");

    TEST_PASS("test_pm_dependency_cycle");
}

// ─── Test: Cross-validate supported_items against dependency universe ─

void test_pm_cross_validate() {
    ProfileManager pm;
    auto& vanilla = pm.create(NSID("vanilla"));
    vanilla.add_equipment(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword",
                                    NSID("#minecraft:sword"), 1561});
    vanilla.add_tag(EquipmentTag{NSID("#minecraft:sword"), "sword"});

    auto& mod = pm.create(NSID("mod"));
    mod.set_dependencies({NSID("vanilla")});

    // Valid refs (tag + concrete item from vanilla) plus one unknown item.
    EnchInfo sharp = make_ench("minecraft:sharpness", "Sharpness", 5);
    sharp.supported_items = {NSID("#minecraft:sword"), NSID("minecraft:diamond_sword"),
                             NSID("minecraft:stone")};
    mod.add_enchantment(sharp);

    // All refs unknown → enchantment must be removed entirely.
    EnchInfo mending = make_ench("minecraft:mending", "Mending", 1);
    mending.supported_items = {NSID("minecraft:netherite_ingot")};
    mod.add_enchantment(mending);

    size_t removed = pm.cross_validate(NSID("mod"));
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
            "dependencies": ["vanilla"],
            "enchantments": [],
            "equipments": [],
            "categories": [],
            "tags": {}
        })";
    }

    pm.load_directory(dir);
    expect(pm.exists(NSID("bare_mod")), "bare_mod loaded by file stem");
    expect(pm.exists(NSID("vanilla")), "vanilla base auto-created");

    // The JSON `dependencies` array must be parsed into the loaded profile.
    Profile* loaded_mod = pm.find(NSID("bare_mod"));
    expect(loaded_mod != nullptr, "loaded bare_mod findable");
    if (loaded_mod) {
        const auto& deps = loaded_mod->dependencies();
        expect(deps.size() == 1 && deps[0] == NSID("vanilla"),
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
    pm.create(NSID("vanilla"));
    auto& mod = pm.create(NSID("enchantencore"));
    mod.set_dependencies({NSID("vanilla")});
    mod.add_enchantment({NSID("mod:leeching"), "Leeching", MCE::All, 2, 2, 1, false, {}, {NSID("#minecraft:swords")}});
    mod.add_tag({NSID("#minecraft:swords"), "swords"});
    auto& pack = pm.create(NSID("mypack"));
    pack.set_dependencies({NSID("enchantencore")});
    // pack overrides leeching's max_level
    pack.add_enchantment({NSID("mod:leeching"), "Leeching", MCE::All, 3, 3, 1, false, {}, {NSID("#minecraft:swords")}});

    auto& eff = pm.resolve_effective(NSID("mypack"));
    expect(eff.ench().contains(NSID("mod:leeching")), "effective view contains dep enchant");
    expect(eff.ench().at(NSID("mod:leeching")).max_level == 3, "pack overrides dep");
    expect(eff.tag_resolver() != nullptr, "effective view carries TagResolver");
    TEST_PASS("test_pm_effective_view");
}

// ─── Test: Manager-level edit (real-time validation) + snapshot/undo ────

void test_pm_edit_snapshot_undo() {
    ProfileManager pm;
    auto& p = pm.create(NSID("test:edit"));
    p.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:swords")}});
    p.add_enchantment({NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false, {NSID("minecraft:sharpness")}, {NSID("#minecraft:swords")}});

    // 实时校验：给 smite 加不存在的 exclusive 引用 → 拒绝（不应用、无快照）
    EnchInfo bad = p.ench().at(NSID("minecraft:smite"));
    bad.exclusive_set.insert(NSID("nonexistent:ench"));
    expect(!pm.update_enchantment(NSID("test:edit"), bad), "invalid exclusive ref rejected");
    // 拒绝的变更未应用
    expect(p.ench().at(NSID("minecraft:smite")).exclusive_set.count(NSID("nonexistent:ench")) == 0,
           "rejected edit leaves profile untouched");

    // 合法编辑 → 应用；undo 回滚
    EnchInfo patch = p.ench().at(NSID("minecraft:sharpness"));
    patch.max_level = 6;
    expect(pm.update_enchantment(NSID("test:edit"), patch), "valid edit applied");
    expect(p.ench().at(NSID("minecraft:sharpness")).max_level == 6, "max_level updated");
    expect(pm.undo(NSID("test:edit")), "undo succeeds");
    expect(p.ench().at(NSID("minecraft:sharpness")).max_level == 5, "undo reverts max_level");

    // 连续两次 undo：第二次应失败（仅回滚最近一次）
    expect(!pm.undo(NSID("test:edit")), "second undo fails (log exhausted)");

    TEST_PASS("test_pm_edit_snapshot_undo");
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
        test_create_empty_profile_structure();
        test_pm_dependency_chain();
        test_pm_dependency_cycle();
        test_pm_cross_validate();
        test_pm_load_directory();
        test_pm_effective_view();
        test_pm_edit_snapshot_undo();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
