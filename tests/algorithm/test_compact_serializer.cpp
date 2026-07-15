#include "framework/test_utils.h"
#include "algorithm/serialization/CompactSerializer.h"
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

} // anonymous namespace

int main() {
    try {
        test_ench_roundtrip();
        test_ench_set_roundtrip();
        test_item_roundtrip();
        test_step_roundtrip();
        test_solution_roundtrip();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
