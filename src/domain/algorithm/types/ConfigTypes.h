#pragma once
#include "common/CommonTypes.h"
#include "common/serialization/IBinarySerializable.h"
#include <chrono>
#include <cstdint>

namespace algorithm {

// ─── Search configuration ─────────────────────────────────────────
struct SearchConfig : IBinarySerializable {
    int32_t max_solutions = 0;
    int32_t max_depth     = 0;
    int32_t memory_mb     = 0;
    uint32_t max_threads  = 0;             // 0 = hardware_concurrency
    int32_t initial_bound = INT32_MAX;     // warm-start bound
    std::chrono::milliseconds max_search_time{180'000}; // default 3 min

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << max_solutions << max_depth << memory_mb << max_threads
          << initial_bound
          << static_cast<int64_t>(max_search_time.count());
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        int64_t t;
        r >> max_solutions >> max_depth >> memory_mb >> max_threads
          >> initial_bound >> t;
        max_search_time = std::chrono::milliseconds(t);
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
