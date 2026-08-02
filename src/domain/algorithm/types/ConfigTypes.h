#pragma once
#include "common/CommonTypes.h"
#include "common/serialization/IBinarySerializable.h"
#include <chrono>
#include <cstdint>
#include <map>

namespace algorithm {

// ─── Search configuration ─────────────────────────────────────────
// Two-tier field contract (see docs/algotithm_designs/search-config-semantics.md):
//   • UNIVERSAL fields  — every strategy is expected to honor these. Where a
//     strategy cannot do so meaningfully (e.g. a DP strategy yields exactly one
//     best solution), it trivially satisfies the field or degrades to a slower
//     warm-up — never to an incorrect result.
//   • ALGORITHM-SPECIFIC fields — honored by the named strategy only; a no-op
//     elsewhere. CLI flags feeding them are documented as such.
struct SearchConfig : IBinarySerializable {
    // ── UNIVERSAL ────────────────────────────────────────────────────
    /// Maximum number of solutions to return. 0 = unlimited.
    /// Honored by AStar (AStarAlgorithm.cpp:371) and DFS (DFSAlgorithm.cpp:257);
    /// single-best strategies (bb_dp, dp_merge, hamming) trivially satisfy N >= 1.
    int32_t max_solutions = 0;

    /// Warm-start upper bound: prune any branch whose cost already exceeds it.
    /// Honored by AStar (AStarAlgorithm.cpp:214), DFS (DFSAlgorithm.cpp:144) and
    /// bb_dp (BBDpAlgorithm.cpp:552); dp_merge/hamming ignore it — a slower
    /// warm-up, not a correctness difference.
    int32_t initial_bound = INT32_MAX;

    /// Global search time budget. Enforced at Executor level
    /// (AlgorithmExecutor.cpp:188) for every strategy, and re-checked inside the
    /// AStar/DFS hot loops. 0 = unlimited.
    std::chrono::milliseconds max_search_time{180'000}; // default 3 min

    // ── ALGORITHM-SPECIFIC (no-op elsewhere) ─────────────────────────
    /// DFS-only: maximum recursion/stack depth limit. 0 = unlimited.
    int32_t max_depth = 0;

    /// AStar-only: memory budget in MB for the open/closed sets.
    /// 0 = auto (defaults to 2048 MB inside AStarAlgorithm.cpp:183).
    int32_t memory_mb = 0;

    /// Parallel strategies only (bb_dp, dp_merge): thread-pool concurrency.
    /// 0 = hardware_concurrency. Single-threaded strategies (astar, dfs,
    /// hamming) ignore it.
    uint32_t max_threads = 0;

    /// bb_dp-only: per-step anvil cost cap (vanilla Too-Expensive threshold =
    /// 39). 0 = disabled. SOFT constraint — bb_dp first tries for a solution
    /// where every step ≤ max_step_cost, then relaxes if none exists.
    int32_t max_step_cost = 39;
    /// bb_dp-only: Pareto-frontier beam width. 0 = exact (unlimited frontier).
    int32_t beam_width = 0;

    /// ALGORITHM-SPECIFIC escape hatch — completely non-universal options that
    /// don't merit a typed field above.  Keys are namespaced by the strategy
    /// that owns them (e.g. "bb_dp.chunk_bits", "idastar.threshold"); each
    /// strategy reads and parses its own keys, every other strategy ignores the
    /// whole map.  Values are raw strings the owning strategy parses itself.
    /// std::map keeps keys sorted → deterministic binary serialization (stable
    /// checkpoint CRC).  Empty by default; adding a key here never changes
    /// existing typed-field semantics.
    std::map<std::string, std::string> extra;

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << max_solutions << max_depth << memory_mb << max_threads
          << initial_bound
          << static_cast<int64_t>(max_search_time.count())
          << max_step_cost << beam_width;
        w << static_cast<uint32_t>(extra.size());
        for (const auto &[k, v] : extra) {
            w << k << v;
        }
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        int64_t t;
        r >> max_solutions >> max_depth >> memory_mb >> max_threads
          >> initial_bound >> t >> max_step_cost >> beam_width;
        max_search_time = std::chrono::milliseconds(t);
        uint32_t n = 0;
        r >> n;
        extra.clear();
        for (uint32_t i = 0; i < n; ++i) {
            std::string k, v;
            r >> k;
            r >> v;
            extra.emplace(std::move(k), std::move(v));
        }
    }
};

// ─── Forge configuration ────────────────────────────────────────────────────
struct ForgeConfig : IBinarySerializable {

    MCE platform             = MCE::Java;
    bool ignore_penalty_cost = false;
    bool ignore_repair_cost  = false; // when true, skip equip+equip repair fee (+2)

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << static_cast<uint8_t>(platform) << static_cast<uint8_t>(ignore_penalty_cost)
          << static_cast<uint8_t>(ignore_repair_cost);
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        uint8_t p, ipc, irc;
        r >> p >> ipc >> irc;
        platform            = static_cast<MCE>(p);
        ignore_penalty_cost = ipc != 0;
        ignore_repair_cost  = irc != 0;
    }
};

// ─── Algorithm configuration — bundles mode + forge + search ──────────────
struct AlgorithmConfig : IBinarySerializable {
    AlgorithmMode mode  = AlgorithmMode::direct;
    ForgeConfig forge;
    SearchConfig search;

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << static_cast<uint8_t>(mode) << forge << search;
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        uint8_t m;
        r >> m >> forge >> search;
        mode = static_cast<AlgorithmMode>(m);
    }
};

}; // namespace algorithm
