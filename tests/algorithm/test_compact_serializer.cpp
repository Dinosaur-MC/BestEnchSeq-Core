#include "framework/test_utils.h"
#include "algorithm/serialization/CompactSerializer.h"
#include "config/ForgeConfig.h"
#include "config/SearchConfig.h"
#include "types/Equipment.h"
#include "types/EnchInfo.h"
#include <climits>
#include <cstring>

namespace {

// ─── Round-trip: compact::Ench ──────────────────────────────────────────

void test_ench_roundtrip() {
    compact::Ench original{42, 5};
    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_ench(r);

    expect(r.ok(), "read_ench should succeed");
    expect(result.id == original.id, "Ench id should match after round-trip");
    expect(result.level == original.level,
           "Ench level should match after round-trip");
    TEST_PASS("test_ench_roundtrip");
}

// ─── Round-trip: compact::EnchSet ───────────────────────────────────────

void test_ench_set_roundtrip() {
    compact::EnchSet original;
    original.insert({1, 3});
    original.insert({2, 5});
    original.insert({7, 2});

    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_ench_set(r);

    expect(r.ok(), "read_ench_set should succeed");
    expect(original.size() == result.size(),
           "EnchSet size should match after round-trip");
    for (size_t i = 0; i < original.size(); ++i) {
        auto o = original.begin()[i];
        auto r2 = result.begin()[i];
        expect(o.id == r2.id && o.level == r2.level,
               "EnchSet element should match after round-trip");
    }
    TEST_PASS("test_ench_set_roundtrip");
}

// ─── Round-trip: compact::Item ──────────────────────────────────────────

void test_item_roundtrip() {
    compact::Item original;
    original.type = compact::ItemType::Equip;
    original.dur = 1561;
    original.ppn = 2;
    original.enchs.insert({3, 4});
    original.enchs.insert({5, 1});

    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_item(r);

    expect(r.ok(), "read_item should succeed");
    expect(original.type == result.type, "Item type should match after round-trip");
    expect(original.dur == result.dur, "Item dur should match after round-trip");
    expect(original.ppn == result.ppn, "Item ppn should match after round-trip");
    expect(original.enchs.size() == result.enchs.size(),
           "Item enchs size should match after round-trip");
    TEST_PASS("test_item_roundtrip");
}

// ─── Round-trip: compact::EnchStep ──────────────────────────────────────

void test_step_roundtrip() {
    compact::Item base;
    base.type = compact::ItemType::Equip;
    base.dur = 1561;
    base.ppn = 1;
    base.enchs.insert({1, 4});

    compact::Item sacrifice;
    sacrifice.type = compact::ItemType::Book;
    sacrifice.dur = 0;
    sacrifice.ppn = 0;
    sacrifice.enchs.insert({1, 5});

    compact::EnchStep original{base, sacrifice, 7};

    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_ench_step(r);

    expect(r.ok(), "read_ench_step should succeed");
    expect(original.base.type == result.base.type,
           "Step base type should match after round-trip");
    expect(original.base.dur == result.base.dur,
           "Step base dur should match after round-trip");
    expect(original.base.ppn == result.base.ppn,
           "Step base ppn should match after round-trip");
    expect(original.sacrifice.type == result.sacrifice.type,
           "Step sacrifice type should match after round-trip");
    expect(original.cost == result.cost,
           "Step cost should match after round-trip");
    TEST_PASS("test_step_roundtrip");
}

// ─── Round-trip: compact::EnchSolution ──────────────────────────────────

void test_solution_roundtrip() {
    compact::EnchSolution original;
    original.total_cost = 42;

    // Build a multi-step solution
    compact::Item base1;
    base1.type = compact::ItemType::Equip;
    base1.dur = 1561;
    base1.ppn = 1;
    base1.enchs.insert({1, 4});

    compact::Item sac1;
    sac1.type = compact::ItemType::Book;
    sac1.dur = 0;
    sac1.ppn = 0;
    sac1.enchs.insert({1, 5});

    original.steps.push_back({base1, sac1, 7});

    compact::Item base2;
    base2.type = compact::ItemType::Equip;
    base2.dur = 1561;
    base2.ppn = 3;
    base2.enchs.insert({1, 5});
    base2.enchs.insert({2, 2});

    compact::Item sac2;
    sac2.type = compact::ItemType::Book;
    sac2.dur = 0;
    sac2.ppn = 0;
    sac2.enchs.insert({3, 1});

    original.steps.push_back({base2, sac2, 5});

    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_ench_solution(r);

    expect(r.ok(), "read_ench_solution should succeed");
    expect(original.total_cost == result.total_cost,
           "Solution total_cost should match after round-trip");
    expect(original.steps.size() == result.steps.size(),
           "Solution steps size should match after round-trip");
    for (size_t i = 0; i < original.steps.size(); ++i) {
        expect(original.steps[i].cost == result.steps[i].cost,
               "Step cost should match after round-trip");
        expect(original.steps[i].base.type == result.steps[i].base.type,
               "Step base type should match after round-trip");
    }
    TEST_PASS("test_solution_roundtrip");
}

// ─── Security boundary: set_fail rejection ─────────────────────────────

void test_set_fail_rejection() {
    uint8_t data[4] = {1, 2, 3, 4};
    ByteStreamReader r(data, 4);
    expect(r.ok(), "fresh reader ok");
    r.set_fail();
    expect(!r.ok(), "reader fails after set_fail");
    expect(r.fail(), "reader.fail() returns true");
    (void)r.u8();
    expect(!r.ok(), "reader stays failed after read");
    TEST_PASS("test_set_fail_rejection");
}

// ─── Security boundary: overflow count in EnchSet ─────────────────────

void test_overflow_count_ench_set() {
    ByteStreamWriter w;
    w.u32(UINT32_MAX);
    compact_serial::write(w, compact::Ench{1, 3});
    ByteStreamReader r(w.data());
    auto result = compact_serial::read_ench_set(r);
    expect(r.fail(), "read_ench_set with huge count sets fail");
    expect(result.empty(), "result should be empty after overflow");
    TEST_PASS("test_overflow_count_ench_set");
}

// ─── Security boundary: overflow count in EnchSolution ────────────────

void test_overflow_count_solution() {
    ByteStreamWriter w;
    w.u32(UINT32_MAX);
    w.i32(42);
    ByteStreamReader r(w.data());
    auto result = compact_serial::read_ench_solution(r);
    expect(r.fail(), "read_ench_solution with huge count sets fail");
    TEST_PASS("test_overflow_count_solution");
}

// ─── Security boundary: incomplete data rejected ──────────────────────

void test_incomplete_data_rejected() {
    ByteStreamWriter w;
    w.u32(5);
    compact_serial::write(w, compact::Ench{1, 3});
    ByteStreamReader r(w.data());
    auto result = compact_serial::read_ench_set(r);
    (void)result;
    expect(r.fail(), "truncated EnchSet should set reader fail");
    TEST_PASS("test_incomplete_data_rejected");
}

void test_forge_config_roundtrip() {
    ForgeConfig original;
    original.ignore_penalty_cost = true;
    original.ignore_repair_cost = false;
    original.ignore_cost_cap = true;
    original.platform = MCE::Bedrock;

    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_forge_config(r);

    expect(r.ok(), "read_forge_config should succeed");
    expect_eq(result.ignore_penalty_cost, true, "ignore_penalty_cost");
    expect_eq(result.ignore_repair_cost, false, "ignore_repair_cost");
    expect_eq(result.ignore_cost_cap, true, "ignore_cost_cap");
    expect_eq(result.platform, MCE::Bedrock, "platform");
    TEST_PASS("test_forge_config_roundtrip");
}

void test_search_config_roundtrip() {
    SearchConfig original;
    original.max_solutions = 10;
    original.max_depth = 20;
    original.memory_mb = 512;
    original.max_search_time = std::chrono::milliseconds(5000);

    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_search_config(r);

    expect(r.ok(), "read_search_config should succeed");
    expect_eq(result.max_solutions, 10, "max_solutions");
    expect_eq(result.max_depth, 20, "max_depth");
    expect_eq(result.memory_mb, 512, "memory_mb");
    expect_eq(result.max_search_time.count(), 5000LL, "max_search_time");
    TEST_PASS("test_search_config_roundtrip");
}

void test_equipment_roundtrip() {
    Equipment original;
    original.name_id = "minecraft:diamond_sword";
    original.name = "Diamond Sword";
    original.category_id = 1;
    original.max_durability = 1561;

    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_equipment(r);

    expect(r.ok(), "read_equipment should succeed");
    expect_eq(result.name_id, original.name_id, "name_id");
    expect_eq(result.name, original.name, "name");
    expect_eq(result.category_id, original.category_id, "category_id");
    expect_eq(result.max_durability, original.max_durability, "max_durability");
    TEST_PASS("test_equipment_roundtrip");
}

void test_ench_info_roundtrip() {
    EnchInfo original;
    original.name_id = "minecraft:sharpness";
    original.name = "Sharpness";
    original.supported_platform = MCE::All;
    original.max_level = 5;
    original.limited_level = 5;
    original.multiplier = 1;
    original.is_treasure = false;
    original.exclusive_set = {"minecraft:bane_of_arthropods", "minecraft:smite"};
    original.applicable_category_ids = {1, 2, 3};

    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_ench_info(r);

    expect(r.ok(), "read_ench_info should succeed");
    expect_eq(result.name_id, original.name_id, "name_id");
    expect_eq(result.name, original.name, "name");
    expect_eq(result.max_level, original.max_level, "max_level");
    expect_eq(result.is_treasure, original.is_treasure, "is_treasure");
    expect(result.exclusive_set == original.exclusive_set, "exclusive_set should match");
    TEST_PASS("test_ench_info_roundtrip");
}

void test_compact_ench_info_roundtrip() {
    compact::EnchInfo original;
    original.mul = 1;
    original.mul_b = 1;
    original.max_lvl = 5;
    original.exc_mask = {1, 2, 3};
    original.applicable = true;

    ByteStreamWriter w;
    compact_serial::write(w, original);

    ByteStreamReader r(w.data());
    auto result = compact_serial::read_compact_ench_info(r);

    expect(r.ok(), "read_compact_ench_info should succeed");
    expect_eq(result.mul, original.mul, "mul");
    expect_eq(result.mul_b, original.mul_b, "mul_b");
    expect_eq(result.max_lvl, original.max_lvl, "max_lvl");
    expect_eq(result.applicable, original.applicable, "applicable");
    TEST_PASS("test_compact_ench_info_roundtrip");
}

} // anonymous namespace

int main() {
    try {
        test_ench_roundtrip();
        test_ench_set_roundtrip();
        test_item_roundtrip();
        test_step_roundtrip();
        test_solution_roundtrip();
        test_set_fail_rejection();
        test_overflow_count_ench_set();
        test_overflow_count_solution();
        test_incomplete_data_rejected();
        test_forge_config_roundtrip();
        test_search_config_roundtrip();
        test_equipment_roundtrip();
        test_ench_info_roundtrip();
        test_compact_ench_info_roundtrip();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
