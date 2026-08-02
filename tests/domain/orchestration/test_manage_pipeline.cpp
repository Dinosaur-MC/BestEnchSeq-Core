#include "framework/test_utils.h"
#include "domain/orchestration/pipelines/ManagePipeline.h"
#include "domain/orchestration/types/ManageRequest.h"
#include "domain/orchestration/types/ManageResult.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/Profile.h"
#include "common/io/json.h"
#include "common/io/FileUtils.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// ─── Helper: write a native JSON carrying one enchantment (`minecraft:leeching`
// referencing the real vanilla `#minecraft:swords` item tag) ────────────

void write_extra_json(const std::filesystem::path& path) {
    std::ofstream f(path);
    f << R"({
        "name": "extra",
        "enchantments": [
          {
            "id": "leeching",
            "name": "Leeching",
            "platform": "java",
            "max_level": 3,
            "multiplier": 2,
            "exclusive_set": [],
            "is_treasure": false,
            "supported_items": ["#minecraft:swords"]
          }
        ],
        "equipments": [],
        "tags": [],
        "categories": []
    })";
}

// ─── Test 1: LoadBuiltin is idempotent ───────────────────────────────

void test_manage_load_builtin_idempotent() {
    ProfileManager pm;
    ProfileLoader loader;

    ManageRequest req;
    req.action = ManageRequest::Action::LoadBuiltin;
    auto r1 = ManagePipeline::run(pm, loader, req);
    expect(r1.success, "load_builtin #1 succeeds");
    expect(pm.list().size() == 1, "one profile after first load");
    expect(pm.active_name() == "builtin:vanilla", "builtin:vanilla is active");

    // Second run is a guarded no-op (exists guard) — must not throw.
    auto r2 = ManagePipeline::run(pm, loader, req);
    expect(r2.success, "load_builtin #2 succeeds");
    expect(pm.list().size() == 1, "still one profile after second load");

    TEST_PASS("test_manage_load_builtin_idempotent");
}

// ─── Test 2: LoadFile merges into the active profile + invalidates cache ──

void test_manage_load_file_merges() {
    ProfileManager pm;
    ProfileLoader loader;

    // LoadBuiltin seeds the vanilla tag universe (incl. `#minecraft:swords`)
    // so the merged enchantment's tag reference survives cross-validation.
    ManageRequest builtin;
    builtin.action = ManageRequest::Action::LoadBuiltin;
    ManagePipeline::run(pm, loader, builtin);

    // Prime the effective-view cache BEFORE the merge: without notify_mutated
    // a later resolve_effective would return this stale view.
    const auto& eff0 = pm.resolve_effective("builtin:vanilla");
    expect(!eff0.ench().contains(NSID("minecraft:leeching")),
           "no leeching before load");

    auto dir = std::filesystem::temp_directory_path() / "besq_manage_loadfile";
    std::filesystem::create_directories(dir);
    auto path = dir / "extra.json";
    write_extra_json(path);

    ManageRequest load;
    load.action = ManageRequest::Action::LoadFile;
    load.file_path = path.string();
    auto res = ManagePipeline::run(pm, loader, load);
    expect(res.success, "LoadFile succeeds");
    expect(pm.active().ench().contains(NSID("minecraft:leeching")),
           "active profile contains merged enchantment");

    // notify_mutated invalidated the cache — the fresh view sees the merge.
    const auto& eff1 = pm.resolve_effective("builtin:vanilla");
    expect(eff1.ench().contains(NSID("minecraft:leeching")),
           "effective view refreshed after notify_mutated");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_manage_load_file_merges");
}

// ─── Test 3: LoadData skips nonexistent filters (exists guard) ────────

void test_manage_load_data_exists_guard() {
    ProfileManager pm;
    ProfileLoader loader;

    ManageRequest builtin;
    builtin.action = ManageRequest::Action::LoadBuiltin;
    ManagePipeline::run(pm, loader, builtin);

    auto dir = std::filesystem::temp_directory_path() / "besq_manage_loaddata";
    std::filesystem::create_directories(dir);
    auto path = dir / "extra.json";
    write_extra_json(path);

    // One nonexistent + one existing path — the exists guard skips the former.
    ManageRequest req;
    req.action = ManageRequest::Action::LoadData;
    req.filters = {(dir / "missing.json").string(), path.string()};
    auto res = ManagePipeline::run(pm, loader, req);
    expect(res.success, "LoadData with a missing filter does not throw");
    expect(pm.active().ench().contains(NSID("minecraft:leeching")),
           "existing filter's content merged in");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_manage_load_data_exists_guard");
}

// ─── Test 4: LoadDirectory loads a CSV profile ────────────────────────

void test_manage_load_directory() {
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() /
               ("besq_manage_dir_" + std::to_string(++counter));
    std::filesystem::create_directories(dir);

    // A CSV profile (established load_directory pattern from
    // test_load_directory_skips_equipments_csv).
    std::ofstream(dir / "pack.csv") <<
        "id,name,max_level,multiplier,exclusive_set,supported_items\n"
        "mod:sharp,Sharp,5,1,,\"#minecraft:swords\"\n";

    ProfileManager pm;
    ProfileLoader loader;
    ManageRequest req;
    req.action = ManageRequest::Action::LoadDirectory;
    req.dir_path = dir.string();
    auto res = ManagePipeline::run(pm, loader, req);
    expect(res.success, "LoadDirectory succeeds");
    expect(pm.exists("pack"), "csv profile loaded under stem key");
    expect(pm.exists("builtin:vanilla"), "vanilla base auto-created");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_manage_load_directory");
}

// ─── Test 5: Profile CRUD actions ─────────────────────────────────────

void test_manage_profile_crud() {
    ProfileManager pm;
    ProfileLoader loader;

    ManageRequest create;
    create.action = ManageRequest::Action::CreateProfile;
    create.profile_name = "p1";
    auto r = ManagePipeline::run(pm, loader, create);
    expect(r.success && pm.exists("p1"), "CreateProfile");

    ManageRequest act;
    act.action = ManageRequest::Action::ActivateProfile;
    act.profile_name = "p1";
    r = ManagePipeline::run(pm, loader, act);
    expect(r.success && pm.active_name() == "p1", "ActivateProfile");

    ManageRequest fork;
    fork.action = ManageRequest::Action::ForkProfile;
    fork.source_name = "p1";
    fork.dest_name = "p1_fork";
    r = ManagePipeline::run(pm, loader, fork);
    expect(r.success && pm.exists("p1_fork"), "ForkProfile");

    ManageRequest merge;
    merge.action = ManageRequest::Action::MergeProfile;
    merge.source_name = "p1_fork";
    merge.dest_name = "p1";
    r = ManagePipeline::run(pm, loader, merge);
    expect(r.success && pm.size() == 2, "MergeProfile");

    ManageRequest list;
    list.action = ManageRequest::Action::ListProfiles;
    r = ManagePipeline::run(pm, loader, list);
    expect(r.success && r.profile_list.size() == 2, "ListProfiles");
    expect(!r.message.empty(), "ListProfiles message non-empty");

    ManageRequest rm;
    rm.action = ManageRequest::Action::RemoveProfile;
    rm.profile_name = "p1_fork";
    r = ManagePipeline::run(pm, loader, rm);
    expect(r.success && !pm.exists("p1_fork") && pm.size() == 1, "RemoveProfile");

    TEST_PASS("test_manage_profile_crud");
}

// ─── Test 6: Registry editing actions (incl. duplicate failure branches) ──

void test_manage_registry_edit() {
    ProfileManager pm;
    ProfileLoader loader;

    ManageRequest create;
    create.action = ManageRequest::Action::CreateProfile;
    create.profile_name = "edit";
    ManagePipeline::run(pm, loader, create);
    ManageRequest act;
    act.action = ManageRequest::Action::ActivateProfile;
    act.profile_name = "edit";
    ManagePipeline::run(pm, loader, act);

    // AddEnchantment — succeeds, duplicate rejected.
    ManageRequest add;
    add.action = ManageRequest::Action::AddEnchantment;
    add.ench_info = EnchInfo{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5,
                             1, false, {}, {}};
    auto r = ManagePipeline::run(pm, loader, add);
    expect(r.success, "AddEnchantment succeeds");
    r = ManagePipeline::run(pm, loader, add);
    expect(!r.success, "duplicate AddEnchantment rejected");

    // ModifyEnchantment — raise max_level (limited_level -1 skips that patch).
    ManageRequest mod;
    mod.action = ManageRequest::Action::ModifyEnchantment;
    mod.profile_name = "sharpness";
    mod.ench_info = EnchInfo{NSID("minecraft:sharpness"), "", MCE::None, 7, -1, 0,
                             false, {}, {}};
    r = ManagePipeline::run(pm, loader, mod);
    expect(r.success, "ModifyEnchantment succeeds");
    expect(pm.active().ench().at(NSID("minecraft:sharpness")).max_level == 7,
           "max_level raised to 7");

    // AddEquipment — succeeds, duplicate rejected.
    ManageRequest add_eq;
    add_eq.action = ManageRequest::Action::AddEquipment;
    add_eq.equip = Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword",
                             EquipmentTag::sword(), 1561};
    r = ManagePipeline::run(pm, loader, add_eq);
    expect(r.success, "AddEquipment succeeds");
    r = ManagePipeline::run(pm, loader, add_eq);
    expect(!r.success, "duplicate AddEquipment rejected");

    // RemoveEquipment.
    ManageRequest rm_eq;
    rm_eq.action = ManageRequest::Action::RemoveEquipment;
    rm_eq.profile_name = "minecraft:diamond_sword";
    r = ManagePipeline::run(pm, loader, rm_eq);
    expect(r.success, "RemoveEquipment succeeds");
    expect(!pm.active().has_equipment(NSID("minecraft:diamond_sword")),
           "equipment removed");

    // RemoveEnchantment.
    ManageRequest rm_ench;
    rm_ench.action = ManageRequest::Action::RemoveEnchantment;
    rm_ench.profile_name = "sharpness";
    r = ManagePipeline::run(pm, loader, rm_ench);
    expect(r.success, "RemoveEnchantment succeeds");
    expect(!pm.active().has_enchantment(NSID("minecraft:sharpness")),
           "enchantment removed");

    // AddCategory — succeeds, duplicate rejected.
    ManageRequest add_cat;
    add_cat.action = ManageRequest::Action::AddCategory;
    add_cat.category_name = "sword";
    r = ManagePipeline::run(pm, loader, add_cat);
    expect(r.success, "AddCategory succeeds");
    r = ManagePipeline::run(pm, loader, add_cat);
    expect(!r.success, "duplicate AddCategory rejected");

    TEST_PASS("test_manage_registry_edit");
}

// ─── Test 7: PublishProfile writes a versioned/tagged file ─────────────

void test_manage_publish_profile() {
    ProfileManager pm;
    ProfileLoader loader;

    // Vanilla base + a profile to publish carrying one enchantment.
    ManageRequest builtin;
    builtin.action = ManageRequest::Action::LoadBuiltin;
    ManagePipeline::run(pm, loader, builtin);

    ManageRequest create;
    create.action = ManageRequest::Action::CreateProfile;
    create.profile_name = "mypack";
    ManagePipeline::run(pm, loader, create);

    ManageRequest act;
    act.action = ManageRequest::Action::ActivateProfile;
    act.profile_name = "mypack";
    ManagePipeline::run(pm, loader, act);

    ManageRequest add;
    add.action = ManageRequest::Action::AddEnchantment;
    add.ench_info = EnchInfo{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5,
                             1, false, {}, {NSID("#minecraft:swords")}};
    auto r = ManagePipeline::run(pm, loader, add);
    expect(r.success, "AddEnchantment before publish");

    auto out = std::filesystem::temp_directory_path() / "besq_manage_publish.json";
    ManageRequest pub;
    pub.action = ManageRequest::Action::PublishProfile;
    pub.profile_name = "mypack";
    pub.publish_version = "1.0.0";
    pub.publish_tag = "stable";
    pub.output_path = out.string();
    r = ManagePipeline::run(pm, loader, pub);
    expect(r.success, "PublishProfile succeeds");
    expect(std::filesystem::exists(out), "publish file written");

    auto json = Json::parse(file_utils::read_file(out));
    expect(json.has("version") && json["version"].as<std::string>() == "1.0.0",
           "version embedded");
    expect(json.has("release_tag") && json["release_tag"].as<std::string>() == "stable",
           "tag embedded");
    expect(json.has("enchantments"), "published file carries enchantments");
    expect(!r.message.empty(), "publish success carries a message");

    // Failure branch: publishing a nonexistent profile fails with a message.
    ManageRequest bad;
    bad.action = ManageRequest::Action::PublishProfile;
    bad.profile_name = "no_such_profile";
    bad.publish_version = "1.0.0";
    bad.publish_tag = "stable";
    bad.output_path = out.string();
    r = ManagePipeline::run(pm, loader, bad);
    expect(!r.success, "publishing a nonexistent profile fails");
    expect(!r.message.empty(), "publish failure carries a message");

    std::filesystem::remove(out);
    TEST_PASS("test_manage_publish_profile");
}

// ─── Test 8: ImportRegistry merges + invalidates the effective cache ────

void test_manage_import_registry() {
    ProfileManager pm;
    ProfileLoader loader;

    // Fork builtin:vanilla so the imported enchantment's `#minecraft:swords`
    // reference resolves against the inherited vanilla tag universe.
    ManageRequest builtin;
    builtin.action = ManageRequest::Action::LoadBuiltin;
    ManagePipeline::run(pm, loader, builtin);

    ManageRequest fork;
    fork.action = ManageRequest::Action::ForkProfile;
    fork.source_name = "builtin:vanilla";
    fork.dest_name = "imp";
    ManagePipeline::run(pm, loader, fork);

    ManageRequest act;
    act.action = ManageRequest::Action::ActivateProfile;
    act.profile_name = "imp";
    ManagePipeline::run(pm, loader, act);

    // Prime the effective-view cache before import.
    const auto& eff0 = pm.resolve_effective("imp");
    expect(!eff0.ench().contains(NSID("minecraft:leeching")),
           "no leeching before import");

    auto dir = std::filesystem::temp_directory_path() / "besq_manage_import";
    std::filesystem::create_directories(dir);
    auto path = dir / "imp.json";
    write_extra_json(path);

    ManageRequest imp;
    imp.action = ManageRequest::Action::ImportRegistry;
    imp.file_path = path.string();
    auto r = ManagePipeline::run(pm, loader, imp);
    expect(r.success, "ImportRegistry succeeds");
    expect(pm.active().ench().contains(NSID("minecraft:leeching")),
           "active profile contains imported enchantment");

    // notify_mutated invalidated the cache — the fresh view sees the merge.
    const auto& eff1 = pm.resolve_effective("imp");
    expect(eff1.ench().contains(NSID("minecraft:leeching")),
           "effective view refreshed after notify_mutated");

    std::filesystem::remove_all(dir);
    TEST_PASS("test_manage_import_registry");
}

} // anonymous namespace

int main() {
    try {
        test_manage_load_builtin_idempotent();
        test_manage_load_file_merges();
        test_manage_load_data_exists_guard();
        test_manage_load_directory();
        test_manage_profile_crud();
        test_manage_registry_edit();
        test_manage_publish_profile();
        test_manage_import_registry();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
