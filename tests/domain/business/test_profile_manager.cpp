#include "framework/test_utils.h"
#include "domain/business/managers/ProfileManager.h"
#include "domain/business/types/Profile.h"
#include "domain/business/registries/EnchantmentRegistry.h"

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
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
