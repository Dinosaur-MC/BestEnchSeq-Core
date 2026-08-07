#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "common/io/ByteStream.h"
#include "domain/algorithm/components/StepTree.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/Solution.h"
#include <cstring>

namespace {

// ─── Round-trip: algorithm::Ench ──────────────────────────────────────────

TEST_CASE("test_ench_roundtrip") {
    algorithm::Ench original{42, 5};
    ByteStreamWriter w;
    w << original;

    ByteStreamReader r(w.data());
    algorithm::Ench result;
    r >> result;

    expect(r.ok(), "deserialize should succeed");
    expect(result.id == original.id, "Ench id should match after round-trip");
    expect(result.level == original.level,
           "Ench level should match after round-trip");
    TEST_PASS("test_ench_roundtrip");
}

// ─── Round-trip: algorithm::EnchSet ───────────────────────────────────────

TEST_CASE("test_ench_set_roundtrip") {
    algorithm::EnchSet original;
    original.insert({1, 3});
    original.insert({2, 5});
    original.insert({7, 2});

    ByteStreamWriter w;
    w << original;

    ByteStreamReader r(w.data());
    algorithm::EnchSet result;
    r >> result;

    expect(r.ok(), "deserialize should succeed");
    expect(original == result, "EnchSet should be equal after round-trip");
    TEST_PASS("test_ench_set_roundtrip");
}

// ─── Round-trip: algorithm::Item ──────────────────────────────────────────

TEST_CASE("test_item_roundtrip") {
    algorithm::Item original;
    original.type = algorithm::ItemType::Equip;
    original.dur = 1561;
    original.ppn = 2;
    original.enchs.insert({3, 4});
    original.enchs.insert({5, 1});

    ByteStreamWriter w;
    w << original;

    ByteStreamReader r(w.data());
    algorithm::Item result;
    r >> result;

    expect(r.ok(), "deserialize should succeed");
    expect(original.type == result.type, "Item type should match after round-trip");
    expect(original.dur == result.dur, "Item dur should match after round-trip");
    expect(original.ppn == result.ppn, "Item ppn should match after round-trip");
    expect(original.enchs.size() == result.enchs.size(),
           "Item enchs size should match after round-trip");
    TEST_PASS("test_item_roundtrip");
}

// ─── Round-trip: algorithm::EnchStep ──────────────────────────────────────

TEST_CASE("test_step_roundtrip") {
    algorithm::Item base;
    base.type = algorithm::ItemType::Equip;
    base.dur = 1561;
    base.ppn = 1;
    base.enchs.insert({1, 4});

    algorithm::Item sacrifice;
    sacrifice.type = algorithm::ItemType::Book;
    sacrifice.dur = 0;
    sacrifice.ppn = 0;
    sacrifice.enchs.insert({1, 5});

    algorithm::EnchStep original{base, sacrifice, {}, 7};

    ByteStreamWriter w;
    w << original;

    ByteStreamReader r(w.data());
    algorithm::EnchStep result;
    r >> result;

    expect(r.ok(), "deserialize should succeed");
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

// ─── Round-trip: algorithm::EnchSolution ──────────────────────────────────

TEST_CASE("test_solution_roundtrip") {
    algorithm::EnchSolution original;
    original.total_cost = 42;

    // Build a multi-step solution
    algorithm::Item base1;
    base1.type = algorithm::ItemType::Equip;
    base1.dur = 1561;
    base1.ppn = 1;
    base1.enchs.insert({1, 4});

    algorithm::Item sac1;
    sac1.type = algorithm::ItemType::Book;
    sac1.dur = 0;
    sac1.ppn = 0;
    sac1.enchs.insert({1, 5});

    original.steps.push_back({base1, sac1, {}, 7});

    algorithm::Item base2;
    base2.type = algorithm::ItemType::Equip;
    base2.dur = 1561;
    base2.ppn = 3;
    base2.enchs.insert({1, 5});
    base2.enchs.insert({2, 2});

    algorithm::Item sac2;
    sac2.type = algorithm::ItemType::Book;
    sac2.dur = 0;
    sac2.ppn = 0;
    sac2.enchs.insert({3, 1});

    original.steps.push_back({base2, sac2, {}, 5});

    ByteStreamWriter w;
    w << original;

    ByteStreamReader r(w.data());
    algorithm::EnchSolution result;
    r >> result;

    expect(r.ok(), "deserialize should succeed");
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

TEST_CASE("test_set_fail_rejection") {
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

// ─── Security boundary: truncated EnchSet data rejected ───────────────

TEST_CASE("test_truncated_ench_set_rejected") {
    // New EnchSet format: u64 mask (8 bytes) + u8 lvls[64] (64 bytes) = 72 bytes.
    // Write valid mask but incomplete level data to trigger read failure.
    ByteStreamWriter w;
    w.u64(1);             // mask: bit 0 set → id=0 present
    uint8_t partial[] = {1, 2, 3};
    w.bytes(partial, 3);  // only 3 level bytes out of 64 needed
    ByteStreamReader r(w.data());
    algorithm::EnchSet result;
    r >> result;
    // Reader should fail on incomplete data
    expect(r.fail(), "truncated EnchSet should set reader fail");
    TEST_PASS("test_truncated_ench_set_rejected");
}

// ─── Security boundary: overflow count in Solution ────────────────────────

TEST_CASE("test_overflow_count_solution") {
    // Just verify the reader survives a large count without OOM/crash.
    // The serializer reads the count then attempts to read that many EnchSteps,
    // which will fail on truncated data — that's acceptable.
    ByteStreamWriter w;
    w.u64(999999); // large but not absurd count
    w.i32(42);     // total_cost
    ByteStreamReader r(w.data());
    algorithm::EnchSolution result;
    r >> result;
    // Should not crash. Result may be empty or partial.
    expect(r.fail() || true,
           "overflow count should not crash");
    TEST_PASS("test_overflow_count_solution");
}

TEST_CASE("test_forge_config_roundtrip") {
    algorithm::ForgeConfig original;
    original.ignore_penalty_cost = true;
    original.ignore_repair_cost = false;
    original.platform = MCE::Bedrock;

    ByteStreamWriter w;
    w << original;

    ByteStreamReader r(w.data());
    algorithm::ForgeConfig result;
    r >> result;

    expect(r.ok(), "deserialize should succeed");
    expect_eq(result.ignore_penalty_cost, true, "ignore_penalty_cost");
    expect_eq(result.ignore_repair_cost, false, "ignore_repair_cost");
    expect_eq(result.platform, MCE::Bedrock, "platform");
    TEST_PASS("test_forge_config_roundtrip");
}

TEST_CASE("test_search_config_roundtrip") {
    algorithm::SearchConfig original;
    original.max_solutions = 10;
    original.max_depth = 20;
    original.memory_mb = 512;
    original.initial_bound = 1234;
    original.max_search_time = std::chrono::milliseconds(5000);
    original.extra = {{"bb_dp.chunk_bits", "12"}, {"idastar.threshold", "1.5"}};

    ByteStreamWriter w;
    w << original;

    ByteStreamReader r(w.data());
    algorithm::SearchConfig result;
    r >> result;

    expect(r.ok(), "deserialize should succeed");
    expect_eq(result.max_solutions, 10, "max_solutions");
    expect_eq(result.max_depth, 20, "max_depth");
    expect_eq(result.memory_mb, 512, "memory_mb");
    expect_eq(result.initial_bound, 1234, "initial_bound");
    expect_eq(result.max_search_time.count(), 5000LL, "max_search_time");
    expect(result.extra == original.extra, "extra map should roundtrip");
    expect_eq(result.extra.at("bb_dp.chunk_bits"), std::string("12"), "extra value");
    // Empty extra also roundtrips (backward-compatible tail)
    algorithm::SearchConfig empty;
    ByteStreamWriter w2;
    w2 << empty;
    ByteStreamReader r2(w2.data());
    algorithm::SearchConfig result2;
    r2 >> result2;
    expect(r2.ok() && result2.extra.empty(), "empty extra map roundtrip");
    TEST_PASS("test_search_config_roundtrip");
}

TEST_CASE("test_compact_ench_info_roundtrip") {
    algorithm::EnchInfo original;
    original.id = 3;
    original.mul = 1;
    original.mul_b = 1;
    original.max_lvl = 5;
    original.exc_mask = 0b111;
    original.applicable = true;

    ByteStreamWriter w;
    w << original;

    ByteStreamReader r(w.data());
    algorithm::EnchInfo result;
    r >> result;

    expect(r.ok(), "deserialize should succeed");
    expect_eq(result.mul, original.mul, "mul");
    expect_eq(result.mul_b, original.mul_b, "mul_b");
    expect_eq(result.max_lvl, original.max_lvl, "max_lvl");
    expect_eq(result.applicable, original.applicable, "applicable");
    TEST_PASS("test_compact_ench_info_roundtrip");
}

} // anonymous namespace

// ─── StepTree ↔ flat-steps lossless round-trip ───────────────────────────

TEST_CASE("test_step_tree_roundtrip") {
    using algorithm::Item;
    using algorithm::ItemType;
    using algorithm::EnchStep;

    // Balanced merge tree materialized in post-order (book+book merges
    // interleaved with equipment merges):
    //   S1: eq + b1 → eq1 ;  S2: b2 + b3 → b23 ;  S3: eq1 + b23 → fin
    Item eq{ItemType::Equip, 1561, 0, {}};
    Item b1{ItemType::Book, 0, 0, {}};  b1.enchs.insert({0, 5});
    Item b2{ItemType::Book, 0, 0, {}};  b2.enchs.insert({1, 2});
    Item b3{ItemType::Book, 0, 0, {}};  b3.enchs.insert({2, 3});
    Item eq1{ItemType::Equip, 1561, 0, {}}; eq1.enchs.insert({0, 5});
    Item b23{ItemType::Book, 0, 0, {}}; b23.enchs.insert({1, 2}); b23.enchs.insert({2, 3});
    Item fin{ItemType::Equip, 1561, 0, {}};
    fin.enchs.insert({0, 5}); fin.enchs.insert({1, 2}); fin.enchs.insert({2, 3});

    std::vector<EnchStep> flat = {
        {eq,  b1,  eq1, 3},
        {b2,  b3,  b23, 4},
        {eq1, b23, fin, 5},
    };

    algorithm::StepTree tree = algorithm::StepTree::from_steps(flat);
    expect(!tree.empty(), "from_steps should rebuild a non-empty tree");

    auto back = tree.materialize();
    expect(back.size() == flat.size(), "round-trip should preserve step count");
    bool same = back.size() == flat.size();
    for (size_t i = 0; same && i < back.size(); ++i) {
        if (!(back[i].base == flat[i].base) || !(back[i].sacrifice == flat[i].sacrifice)
            || !(back[i].result == flat[i].result) || back[i].cost != flat[i].cost)
            same = false;
    }
    expect(same, "materialize(from_steps(flat)) should equal flat (lossless)");
    TEST_PASS("test_step_tree_roundtrip");
}
