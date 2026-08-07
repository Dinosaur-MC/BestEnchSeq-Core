// =============================================================================
// ForgeEngine Microbenchmark
//
// Benchmarks individual ForgeEngine operations (forge_into, forge,
// pure_forge_into, estimate_forge_cost) across a matrix of input sizes,
// operation types, and configurations.
//
// Migrated to the shared benchmark harness (benchmarks/framework/
// bench_framework.h): one BENCH_CASE per (config × op × size) point; each
// body is a single operation call (the old bench() warmup/calibration/
// rounds machinery is replaced by the harness's adaptive warmup/iterations/
// statistics).  The 102 points are registered data-driven at static init.
//
// Build: cmake --build build --target forge_engine_benchmark
// Run:   build/bin/forge_engine_benchmark [--list|--filter ...|--json ...]
// =============================================================================

#define BESQ_BENCH_MAIN
#include "framework/bench_framework.h"

#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/ConfigTypes.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace algorithm;

// Optimization barrier: prevents dead-code elimination of benchmark results
static volatile int64_t bench_sink = 0;

// ══════════════════════════════════════════════════════════════════════════
// Synthetic registry — small controlled set of enchantments
// ══════════════════════════════════════════════════════════════════════════

/// Enchantment template for registry construction.
struct EnchTemplate {
    int16_t local_id;      // assigned ID in compact registry
    uint16_t mul, mul_b;   // cost multipliers
    uint16_t max_lvl;      // max enchantment level
    std::vector<int16_t> conflicts; // IDs of conflicting enchants
};

// Build a compact EnchReg from templates. Returns the registry plus a
// mapping from template index → local_id (which equals the template index
// when all enchants are applicable).
struct TestRegistry {
    EnchReg reg;
    int16_t count; // number of enchantments
};

TestRegistry make_test_registry(int16_t ench_count) {
    // Minimum viable registry: at least 6 enchants so we have variety
    int16_t n = std::max<int16_t>(ench_count, 6);

    std::vector<EnchInfo> infos;
    std::vector<NSID> global_ids;

    for (int16_t i = 0; i < n; ++i) {
        EnchInfo ei;
        ei.id      = static_cast<uint8_t>(i);
        ei.mul     = static_cast<uint8_t>((i % 2 == 0) ? 1 : 2);
        ei.mul_b   = static_cast<uint8_t>(std::max(1, ei.mul >> 1));
        ei.max_lvl = static_cast<uint8_t>(3 + (i % 3)); // 3,4,5 cycling
        ei.applicable = true;

        // Conflicts: even IDs conflict with (even+2) mod n
        if (i % 2 == 0) {
            int16_t foe = (i + 2) % n;
            // Pre-conflict with all previously added enchants
            for (size_t j = 0; j < infos.size(); ++j) {
                if (j == static_cast<size_t>(foe) || j == static_cast<size_t>((i > 1) ? i - 2 : n - 2)) {
                    ei.exc_mask |= 1ULL << j;
                }
            }
        }

        infos.push_back(std::move(ei));
        global_ids.emplace_back("bench", "ench_" + std::to_string(i));
    }

    // Equipment with all enchants applicable (ids 0..n-1)
    Equipment equip;
    equip.id = NSID("bench:dummy_equip");
    equip.max_durability = 500;
    for (int16_t i = 0; i < n; ++i)
        equip.applicable_enchs.insert(i);

    EnchReg reg;
    reg.init(std::move(infos), std::move(global_ids), equip);

    return {std::move(reg), n};
}

// ══════════════════════════════════════════════════════════════════════════
// Item builders
// ══════════════════════════════════════════════════════════════════════════

Item make_target(ItemType type, int16_t dur, uint8_t ppn, EnchSet enchs) {
    Item item;
    item.type = type;
    item.dur  = dur;
    item.ppn  = ppn;
    item.enchs = std::move(enchs);
    return item;
}

Item make_equip(int16_t dur, uint8_t ppn, EnchSet enchs) {
    return make_target(ItemType::Equip, dur, ppn, std::move(enchs));
}

Item make_book(EnchSet enchs) {
    return make_target(ItemType::Book, 0, 0, std::move(enchs));
}

// Generate N enchant IDs sequentially starting from `base`
EnchSet generate_enchants(int16_t base, int16_t count, int16_t max_level = 3) {
    EnchSet s;
    for (int16_t i = 0; i < count; ++i)
        s.insert(Ench{static_cast<Ench::value_type>(base + i), static_cast<Ench::value_type>(1 + (i %max_level))});
    return s;
}

// ══════════════════════════════════════════════════════════════════════════
// Test case definitions
// ══════════════════════════════════════════════════════════════════════════

enum class OpType {
    Merge,      // all sacrifice enchants are new to target
    Upgrade,    // all sacrifice enchants exist on target, need level-up
    Conflict,   // sacrifice contains conflicting enchants
    Mixed,      // 50% merge + 25% upgrade + 25% conflict
};

struct SizeSpec {
    std::string label;
    int16_t target_ench_count;
    int16_t sac_ench_count;
};

const SizeSpec SIZES[] = {
    {"S", 1, 1},
    {"M", 5, 5},
    {"L", 7, 10},
};

// Item pair type for forge operations.
enum class ItemPair { BookBook, EquipBook, EquipEquip };

// Build target & sacrifice items for a given operation type and size.
// `reg_n` is the total number of enchants in the registry.
struct TestPair {
    Item target;
    Item sacrifice;
    std::string label;
};

static Item make_item(ItemPair pair_type, bool is_target, int16_t dur, uint8_t ppn, EnchSet enchs) {
    if (pair_type == ItemPair::BookBook)
        return make_target(ItemType::Book, 0, 0, std::move(enchs));
    if (pair_type == ItemPair::EquipBook)
        return is_target ? make_equip(dur, ppn, std::move(enchs))
                         : make_book(std::move(enchs));
    // EquipEquip
    return make_equip(dur, ppn, std::move(enchs));
}

static std::string pair_label(ItemPair p) {
    switch (p) {
        case ItemPair::BookBook:   return "book+book";
        case ItemPair::EquipBook:  return "equip+book";
        case ItemPair::EquipEquip: return "equip+equip";
    }
    return "?";
}

TestPair make_pair(OpType op, const SizeSpec& size, int16_t reg_n, ItemPair pair_type) {
    int16_t tgt_n = size.target_ench_count;
    int16_t sac_n = size.sac_ench_count;
    std::string op_name = [&]() -> std::string {
        switch (op) {
            case OpType::Merge:    return "Merge";
            case OpType::Upgrade:  return "Upgrade";
            case OpType::Conflict: return "Conflict";
            case OpType::Mixed:    return "Mixed";
        }
        return "?";
    }();
    std::string label = op_name + "/" + size.label;

    switch (op) {
    case OpType::Merge: {
        auto target_enchs = generate_enchants(0, tgt_n);
        auto sac_enchs    = generate_enchants(tgt_n, sac_n);
        Item target = make_item(pair_type, true, 500, 0, std::move(target_enchs));
        Item sac    = make_item(pair_type, false, 500, 0, std::move(sac_enchs));
        return {std::move(target), std::move(sac), label};
    }
    case OpType::Upgrade: {
        auto target_enchs = generate_enchants(0, tgt_n, 3);
        EnchSet sac_enchs;
        for (int16_t i = 0; i < sac_n; ++i) {
            int16_t eid = i % tgt_n;
            int16_t lvl = 2 + (i % 2);
            sac_enchs.insert(Ench{static_cast<Ench::value_type>(eid), static_cast<Ench::value_type>(lvl)});
        }
        Item target = make_item(pair_type, true, 500, 1, std::move(target_enchs));
        Item sac    = make_item(pair_type, false, 500, 1, std::move(sac_enchs));
        return {std::move(target), std::move(sac), label};
    }
    case OpType::Conflict: {
        auto target_enchs = generate_enchants(0, tgt_n);
        EnchSet sac_enchs;
        int16_t half = sac_n / 2;
        for (int16_t i = 0; i < half; ++i) {
            int16_t eid = static_cast<int16_t>(((i * 2) + 2) % reg_n);
            sac_enchs.insert(Ench{static_cast<Ench::value_type>(eid), static_cast<Ench::value_type>(1 + (i %3))});
        }
        for (int16_t i = half; i < sac_n; ++i) {
            int16_t eid = static_cast<int16_t>((i * 2 + 1) % reg_n);
            sac_enchs.insert(Ench{static_cast<Ench::value_type>(eid), static_cast<Ench::value_type>(1 + (i %3))});
        }
        Item target = make_item(pair_type, true, 500, 0, std::move(target_enchs));
        Item sac    = make_item(pair_type, false, 500, 0, std::move(sac_enchs));
        return {std::move(target), std::move(sac), label};
    }
    case OpType::Mixed: {
        auto target_enchs = generate_enchants(0, tgt_n);
        EnchSet sac_enchs;
        int16_t upgrade_n = sac_n / 4;
        int16_t conflict_n = sac_n / 4;
        int16_t merge_n = sac_n - upgrade_n - conflict_n;
        int16_t pos = 0;
        for (int16_t i = 0; i < upgrade_n && pos < tgt_n; ++i, ++pos)
            sac_enchs.insert(Ench{static_cast<Ench::value_type>(pos), 5});
        for (int16_t i = 0; i < conflict_n; ++i, ++pos)
            sac_enchs.insert(Ench{static_cast<Ench::value_type>(((pos * 2) + 2) % reg_n),
                                  static_cast<Ench::value_type>(1 + (i %3))});
        for (int16_t i = 0; i < merge_n; ++i, ++pos)
            sac_enchs.insert(Ench{static_cast<Ench::value_type>(tgt_n + (pos % (reg_n - tgt_n))),
                                  static_cast<Ench::value_type>(1 + (i %3))});
        Item target = make_item(pair_type, true, 500, 1, std::move(target_enchs));
        Item sac    = make_item(pair_type, false, 500, 1, std::move(sac_enchs));
        return {std::move(target), std::move(sac), label};
    }
    }
    return {}; // unreachable
}

// ══════════════════════════════════════════════════════════════════════════
// Per-operation runners
// ══════════════════════════════════════════════════════════════════════════

// Each runner returns a lambda that mutates its captures each call, so the
// compiler cannot fold the operation across iterations.  One invocation of
// the runner is one harness measurement unit.

auto make_forge_into_runner(const ForgeEngine& engine, const EnchReg& reg,
                             const Item& target, const Item& sacrifice) {
    // Copy target each iteration so forge_into() mutates a fresh copy
    return [&engine, &reg, target, sacrifice]() {
        Item t = target;
        bench_sink += engine.forge_into(t, sacrifice, reg);
    };
}

auto make_forge_runner(const ForgeEngine& engine, const EnchReg& reg,
                        const Item& target, const Item& sacrifice) {
    return [&engine, &reg, target, sacrifice]() {
        auto result = engine.forge(target, sacrifice, reg);
        bench_sink += result.second;
    };
}

auto make_pure_forge_runner(const ForgeEngine& engine, const EnchReg& reg,
                             const Item& target, const Item& sacrifice) {
    return [&engine, &reg, target, sacrifice]() {
        Item t = target;
        engine.pure_forge_into(t, sacrifice, reg);
        bench_sink += t.enchs.size() + t.ppn;
    };
}

auto make_estimate_runner(const ForgeEngine& engine, const EnchReg& reg,
                           const Item& target, const Item& sacrifice) {
    return [&engine, &reg, target, sacrifice]() {
        bench_sink += engine.estimate_forge_cost(target, sacrifice, reg);
    };
}

// ══════════════════════════════════════════════════════════════════════════
// Per-configuration context — built once at static init.
// One ForgeEngine per (platform × item-pair) config, plus every test pair
// prebuilt.  The runners hold references into this stable context.
// ══════════════════════════════════════════════════════════════════════════

struct PerfConfig {
    ForgeEngine engine;
    ItemPair pair_type;
    std::string label;
    TestPair pair[4][3];       // [op][size]
    TestPair estimate_pair;    // Merge/S
    TestPair copy_pair;        // Merge/M
};

struct BenchContext {
    EnchReg reg;
    int16_t reg_n = 0;
    std::vector<PerfConfig> configs;
};

static const BenchContext& bench_context() {
    static const std::unique_ptr<const BenchContext> ctx = [] {
        auto c = std::make_unique<BenchContext>();

        // Synthetic registry: 32 enchants gives enough range for all sizes
        auto [reg, reg_n] = make_test_registry(32);
        c->reg = std::move(reg);
        c->reg_n = reg_n;

        for (auto plat : {MCE::Java, MCE::Bedrock}) {
            for (auto pt : {ItemPair::BookBook, ItemPair::EquipBook, ItemPair::EquipEquip}) {
                ForgeConfig fcfg;
                fcfg.platform = plat;
                fcfg.ignore_penalty_cost = false;
                fcfg.ignore_repair_cost = false;

                PerfConfig cfg;
                cfg.engine = ForgeEngine(fcfg);
                cfg.pair_type = pt;
                cfg.label = (plat == MCE::Java ? "Java" : "Bedrock")
                          + std::string(", ") + pair_label(pt);
                for (int op = 0; op < 4; ++op)
                    for (int sz = 0; sz < 3; ++sz)
                        cfg.pair[op][sz] = make_pair(static_cast<OpType>(op), SIZES[sz], reg_n, pt);
                cfg.estimate_pair = make_pair(OpType::Merge, SIZES[0], reg_n, pt);
                cfg.copy_pair     = make_pair(OpType::Merge, SIZES[1], reg_n, pt);
                c->configs.push_back(std::move(cfg));
            }
        }
        return c;
    }();
    return *ctx;
}

// ══════════════════════════════════════════════════════════════════════════
// Registration — the operation matrix (6 configs × 17 points = 102 cases)
// is data-driven: the BENCH_CASE macro registers free functions only, so a
// compile-time matrix is expressed by pushing into the harness registry
// (the same registry the macro feeds).  Names keep the old point semantics:
//   "<config> forge_into/<Op>/<Size>" / pure_forge_into / estimate_forge_cost
//   / forge(copy).
//
// One measurement unit = a fixed batch of K_OPS runner calls (the old
// bench() also timed a loop of runner calls, with its iters calibration
// replaced by the harness's adaptive iteration count).  A single runner
// call (~10-300 ns) is far below the timer granularity, so batching keeps
// the reported medians meaningful; per-op time = reported median / K_OPS.
// ══════════════════════════════════════════════════════════════════════════

constexpr int64_t K_OPS = 1000;

static std::function<void()> make_batch_runner(std::function<void()> single) {
    return [r = std::move(single)]() {
        for (int64_t i = 0; i < K_OPS; ++i)
            r();
    };
}

[[maybe_unused]] static const bool s_registered = [] {
    const BenchContext& b = bench_context();
    auto& reg = ::bench::registry();

    const char* op_names[] = {"Merge", "Upgrade", "Conflict", "Mixed"};
    const std::string batch_suffix = " (batch " + std::to_string(K_OPS) + " ops)";

    for (const auto& cfg : b.configs) {
        for (int op = 0; op < 4; ++op) {
            for (int sz = 0; sz < 3; ++sz) {
                const TestPair& pair = cfg.pair[op][sz];
                reg.push_back(bench::Case{
                    cfg.label + " forge_into/" + op_names[op] + "/" + SIZES[sz].label + batch_suffix,
                    make_batch_runner(make_forge_into_runner(cfg.engine, b.reg, pair.target, pair.sacrifice)),
                    {}, {}, 0});
                // pure_forge_into is measured for Merge only
                if (op == 0) {
                    reg.push_back(bench::Case{
                        cfg.label + " pure_forge_into/" + SIZES[sz].label + batch_suffix,
                        make_batch_runner(make_pure_forge_runner(cfg.engine, b.reg, pair.target, pair.sacrifice)),
                        {}, {}, 0});
                }
            }
        }
        reg.push_back(bench::Case{
            cfg.label + " estimate_forge_cost/S" + batch_suffix,
            make_batch_runner(make_estimate_runner(cfg.engine, b.reg, cfg.estimate_pair.target, cfg.estimate_pair.sacrifice)),
            {}, {}, 0});
        // forge() copy overhead — compare with forge_into/Merge/M
        reg.push_back(bench::Case{
            cfg.label + " forge(copy)/Merge/M" + batch_suffix,
            make_batch_runner(make_forge_runner(cfg.engine, b.reg, cfg.copy_pair.target, cfg.copy_pair.sacrifice)),
            {}, {}, 0});
    }
    return true;
}();

} // namespace
