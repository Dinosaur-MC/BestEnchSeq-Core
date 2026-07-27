// =============================================================================
// ForgeEngine Microbenchmark
//
// Benchmarks individual ForgeEngine operations (forge_into, forge,
// pure_forge_into, estimate_forge_cost) across a matrix of input sizes,
// operation types, and configurations.
//
// Build: cmake --build build --target forge_engine_benchmark
// Run:   build/bin/forge_engine_benchmark [--config ...]
// =============================================================================

#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/ConfigTypes.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace algorithm;
using Clock = std::chrono::steady_clock;

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
        // mul cycles 1,2,1,2,...; mul_b is max(1, mul>>1)
        ei.mul     = static_cast<uint16_t>((i % 2 == 0) ? 1 : 2);
        ei.mul_b   = static_cast<uint16_t>(std::max(1, ei.mul >> 1));
        ei.max_lvl = static_cast<uint16_t>(3 + (i % 3)); // 3,4,5 cycling
        ei.applicable = true;

        // Conflicts: even IDs conflict with (even+2) mod n
        if (i % 2 == 0) {
            int16_t foe = (i + 2) % n;
            ei.exc_mask.resize(infos.size() / MASK_ELEM_SIZE + 1, 0);
            // Pre-conflict with all previously added enchants
            for (size_t j = 0; j < infos.size(); ++j) {
                if (j == static_cast<size_t>(foe) || j == static_cast<size_t>((i > 1) ? i - 2 : n - 2)) {
                    size_t w = j / MASK_ELEM_SIZE;
                    size_t b = j % MASK_ELEM_SIZE;
                    ei.exc_mask[w] |= (MaskType(1) << b);
                    if (w < infos[j].exc_mask.size())
                        infos[j].exc_mask[w] |= (MaskType(1) << b);
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
        s.insert(Ench{static_cast<int16_t>(base + i), static_cast<int16_t>(1 + (i % max_level))});
    return s;
}

// ══════════════════════════════════════════════════════════════════════════
// Benchmark harness
// ══════════════════════════════════════════════════════════════════════════

struct BenchResult {
    std::string name;
    int64_t ns_per_op;
    double  ops_per_sec;  // million ops/sec
};

// Determine iteration count so the batch runs for ~200ms
int determine_iterations(double expected_ns) {
    int iters = static_cast<int>(200'000'000.0 / expected_ns);
    iters = std::clamp(iters, 100, 5'000'000);
    return iters;
}

template <typename Fn>
BenchResult bench(const std::string& name, Fn&& fn, int iterations) {
    // Warmup: 3 runs discarded
    for (int i = 0; i < 3; ++i)
        fn();

    // Timed run
    auto start = Clock::now();
    for (int i = 0; i < iterations; ++i)
        fn();
    auto end  = Clock::now();
    auto ns   = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    int64_t ns_per_op = ns / iterations;
    double ops_per_sec = (iterations * 1'000'000'000.0) / ns / 1'000'000.0; // M ops/s

    return {name, ns_per_op, ops_per_sec};
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

// Build target & sacrifice items for a given operation type and size.
// `reg_n` is the total number of enchants in the registry.
struct TestPair {
    Item target;
    Item sacrifice;
    std::string label;
};

TestPair make_pair(OpType op, const SizeSpec& size, int16_t reg_n, bool book_to_equip) {
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
        // target: enchants 0..tgt_n-1
        // sacrifice: enchants tgt_n..tgt_n+sac_n-1 (all new)
        auto target_enchs = generate_enchants(0, tgt_n);
        auto sac_enchs    = generate_enchants(tgt_n, sac_n);
        Item target = make_equip(500, 0, std::move(target_enchs));
        Item sac    = book_to_equip ? make_book(std::move(sac_enchs))
                                    : make_equip(500, 0, std::move(sac_enchs));
        return {std::move(target), std::move(sac), label};
    }
    case OpType::Upgrade: {
        // target & sacrifice share the same enchant IDs;
        // sacrifice has same or higher levels
        auto target_enchs = generate_enchants(0, tgt_n, 3);
        EnchSet sac_enchs;
        for (int16_t i = 0; i < sac_n; ++i) {
            int16_t eid = i % tgt_n;
            int16_t lvl = 2 + (i % 2); // level 2 or 3
            sac_enchs.insert(Ench{eid, lvl});
        }
        Item target = make_equip(500, 1, std::move(target_enchs));
        Item sac    = book_to_equip ? make_book(std::move(sac_enchs))
                                    : make_equip(500, 1, std::move(sac_enchs));
        return {std::move(target), std::move(sac), label};
    }
    case OpType::Conflict: {
        // target has even IDs; sacrifice has even+2 (conflict) + odd (safe)
        auto target_enchs = generate_enchants(0, tgt_n);
        EnchSet sac_enchs;
        int16_t half = sac_n / 2;
        for (int16_t i = 0; i < half; ++i) {
            // Conflict: add (even+2) % reg_n
            int16_t eid = static_cast<int16_t>(((i * 2) + 2) % reg_n);
            sac_enchs.insert(Ench{eid, static_cast<int16_t>(1 + (i % 3))});
        }
        for (int16_t i = half; i < sac_n; ++i) {
            // Safe odd enchants that are new
            int16_t eid = static_cast<int16_t>((i * 2 + 1) % reg_n);
            sac_enchs.insert(Ench{eid, static_cast<int16_t>(1 + (i % 3))});
        }
        Item target = make_equip(500, 0, std::move(target_enchs));
        Item sac    = book_to_equip ? make_book(std::move(sac_enchs))
                                    : make_equip(500, 0, std::move(sac_enchs));
        return {std::move(target), std::move(sac), label};
    }
    case OpType::Mixed: {
        // ~50% merge, ~25% upgrade, ~25% conflict
        auto target_enchs = generate_enchants(0, tgt_n);
        EnchSet sac_enchs;
        int16_t upgrade_n = sac_n / 4;
        int16_t conflict_n = sac_n / 4;
        int16_t merge_n = sac_n - upgrade_n - conflict_n;
        int16_t pos = 0;
        // upgrade: reuse target enchants with higher levels
        for (int16_t i = 0; i < upgrade_n && pos < tgt_n; ++i, ++pos)
            sac_enchs.insert(Ench{static_cast<int16_t>(pos), 5});
        // conflict: use conflicting IDs
        for (int16_t i = 0; i < conflict_n; ++i, ++pos)
            sac_enchs.insert(Ench{static_cast<int16_t>(((pos * 2) + 2) % reg_n),
                                  static_cast<int16_t>(1 + (i % 3))});
        // merge: use new IDs beyond target range
        for (int16_t i = 0; i < merge_n; ++i, ++pos)
            sac_enchs.insert(Ench{static_cast<int16_t>(tgt_n + (pos % (reg_n - tgt_n))),
                                  static_cast<int16_t>(1 + (i % 3))});
        Item target = make_equip(500, 1, std::move(target_enchs));
        Item sac    = book_to_equip ? make_book(std::move(sac_enchs))
                                    : make_equip(500, 1, std::move(sac_enchs));
        return {std::move(target), std::move(sac), label};
    }
    }
    return {}; // unreachable
}

// ══════════════════════════════════════════════════════════════════════════
// Per-operation runners
// ══════════════════════════════════════════════════════════════════════════

// Each runner returns a lambda that mutates its captures each call, so the
// compiler cannot fold the operation across iterations.

auto make_forge_into_runner(const ForgeEngine& engine, const EnchReg& reg,
                             const Item& target, const Item& sacrifice) {
    // Copy target each iteration so forge_into() mutates a fresh copy
    return [&engine, &reg, target, sacrifice]() {
        Item t = target;
        volatile int32_t cost = engine.forge_into(t, sacrifice, reg);
        (void)cost;
    };
}

auto make_forge_runner(const ForgeEngine& engine, const EnchReg& reg,
                        const Item& target, const Item& sacrifice) {
    return [&engine, &reg, target, sacrifice]() {
        volatile auto result = engine.forge(target, sacrifice, reg);
        (void)result;
    };
}

auto make_pure_forge_runner(const ForgeEngine& engine, const EnchReg& reg,
                             const Item& target, const Item& sacrifice) {
    return [&engine, &reg, target, sacrifice]() {
        Item t = target;
        engine.pure_forge_into(t, sacrifice, reg);
    };
}

auto make_estimate_runner(const ForgeEngine& engine, const EnchReg& reg,
                           const Item& target, const Item& sacrifice) {
    return [&engine, &reg, target, sacrifice]() {
        volatile int32_t cost = engine.estimate_forge_cost(target, sacrifice, reg);
        (void)cost;
    };
}

// ══════════════════════════════════════════════════════════════════════════
// Reporting
// ══════════════════════════════════════════════════════════════════════════

void print_header(const std::string& config_label) {
    std::cout << "\n[" << config_label << "]\n";
    std::cout << std::left << std::setw(22) << "Benchmark"
              << std::right << std::setw(8) << "Iters"
              << std::setw(12) << "ns/op"
              << std::setw(14) << "M ops/s" << "\n";
    std::cout << std::string(56, '-') << "\n";
}

void print_result(const BenchResult& r, int iters) {
    std::cout << std::left << std::setw(22) << r.name
              << std::right << std::setw(8) << iters
              << std::setw(12) << r.ns_per_op
              << std::setw(14) << std::fixed << std::setprecision(2) << r.ops_per_sec
              << "\n";
}

// ══════════════════════════════════════════════════════════════════════════
// Configuration
// ══════════════════════════════════════════════════════════════════════════

struct Config {
    bool run_java      = true;
    bool run_bedrock   = true;
    bool run_book_eq   = true;
    bool run_eq_eq     = true;
};

Config parse_cli(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help") {
            std::cout << "ForgeEngine Benchmark\n";
            std::cout << "Usage: forge_engine_benchmark [options]\n";
            std::cout << "  --java       Java platform only\n";
            std::cout << "  --bedrock    Bedrock platform only\n";
            std::cout << "  --book       Book->equip only\n";
            std::cout << "  --equip      Equip->equip only\n";
            std::cout << "  --help       This help\n";
            exit(0);
        } else if (a == "--java")      { cfg.run_bedrock = false; }
        else if (a == "--bedrock")     { cfg.run_java = false; }
        else if (a == "--book")        { cfg.run_eq_eq = false; }
        else if (a == "--equip")       { cfg.run_book_eq = false; }
    }
    return cfg;
}

// ══════════════════════════════════════════════════════════════════════════
// Main
// ══════════════════════════════════════════════════════════════════════════

} // anonymous namespace

int main(int argc, char* argv[]) {
    auto cfg = parse_cli(argc, argv);

    // Build synthetic registry: 32 enchants gives enough range for all sizes
    auto [reg, reg_n] = make_test_registry(32);

    std::cout << "=== ForgeEngine Benchmark ===\n"
              << "Registry: " << reg_n << " enchants\n"
              << "Configurations: "
              << (cfg.run_java ? "Java " : "")
              << (cfg.run_bedrock ? "Bedrock " : "")
              << (cfg.run_book_eq ? "book→equip " : "")
              << (cfg.run_eq_eq ? "equip→equip " : "")
              << "\n";

    // ── Configurations to run ──────────────────────────────────────────
    struct RunConfig {
        std::string label;
        ForgeConfig fcfg;
        bool book_to_equip;
    };

    std::vector<RunConfig> configs;
    for (auto plat : {MCE::Java, MCE::Bedrock}) {
        if (plat == MCE::Java && !cfg.run_java) continue;
        if (plat == MCE::Bedrock && !cfg.run_bedrock) continue;
        for (bool book_to_equip : {true, false}) {
            if (book_to_equip && !cfg.run_book_eq) continue;
            if (!book_to_equip && !cfg.run_eq_eq) continue;

            ForgeConfig fcfg;
            fcfg.platform = plat;
            fcfg.ignore_penalty_cost = false;
            fcfg.ignore_repair_cost = false;

            std::string label = (plat == MCE::Java ? "Java" : "Bedrock")
                              + std::string(", ") + (book_to_equip ? "book→equip" : "equip→equip");

            configs.push_back({label, fcfg, book_to_equip});
        }
    }

    // ── Estimate baseline (single small merge) to size iterations ──────
    {
        ForgeEngine engine;
        auto pair = make_pair(OpType::Merge, SIZES[0], reg_n, true);
        auto runner = make_forge_into_runner(engine, reg, pair.target, pair.sacrifice);
        auto start = Clock::now();
        int est_n = 100'000;
        for (int i = 0; i < est_n; ++i)
            runner();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        int base_iters = determine_iterations(static_cast<double>(ns) / est_n);
        std::cout << "Base iterations: " << base_iters << "\n\n";

        // ── Run each configuration ────────────────────────────────────────
        for (auto& rc : configs) {
            ForgeEngine engine(rc.fcfg);
            print_header(rc.label);

            for (auto op : {OpType::Merge, OpType::Upgrade, OpType::Conflict, OpType::Mixed}) {
                for (auto& size : SIZES) {
                    auto pair = make_pair(op, size, reg_n, rc.book_to_equip);

                    // forge_into
                    {
                        auto runner = make_forge_into_runner(engine, reg, pair.target, pair.sacrifice);
                        auto r = bench("forge_into/" + pair.label, runner, base_iters);
                        print_result(r, base_iters);
                    }

                    // pure_forge_into (only for merge/pure subset)
                    if (op == OpType::Merge) {
                        auto runner = make_pure_forge_runner(engine, reg, pair.target, pair.sacrifice);
                        auto r = bench("pure_frge/" + pair.label, runner, base_iters * 2);
                        print_result(r, base_iters * 2);
                    }
                }
            }

            // estimate_forge_cost — use the Merge/S pair (fast)
            {
                auto pair = make_pair(OpType::Merge, SIZES[0], reg_n, rc.book_to_equip);
                auto runner = make_estimate_runner(engine, reg, pair.target, pair.sacrifice);
                auto r = bench("estimate/S", runner, base_iters * 5);
                print_result(r, base_iters * 5);
            }

            // forge() copy overhead — compare with forge_into for Merge/M
            {
                auto pair = make_pair(OpType::Merge, SIZES[1], reg_n, rc.book_to_equip);
                auto into_runner = make_forge_into_runner(engine, reg, pair.target, pair.sacrifice);
                auto forge_runner = make_forge_runner(engine, reg, pair.target, pair.sacrifice);
                auto r_into  = bench("forge_into/M", into_runner, base_iters);
                auto r_forge = bench("forge(copy)/M", forge_runner, base_iters);
                print_result(r_into, base_iters);
                print_result(r_forge, base_iters);
                double copy_overhead = (r_forge.ns_per_op - r_into.ns_per_op) * 100.0 / r_into.ns_per_op;
                std::cout << std::left << std::setw(22) << "  copy overhead"
                          << std::right << std::setw(20)
                          << "+" + std::to_string(static_cast<int>(copy_overhead)) + "%"
                          << "\n";
            }
        }
    }

    std::cout << "\n=== Done ===\n";
    return 0;
}
