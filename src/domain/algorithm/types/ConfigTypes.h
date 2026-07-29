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
    std::chrono::milliseconds max_search_time{0};

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << max_solutions << max_depth << memory_mb << max_threads
          << static_cast<int64_t>(max_search_time.count());
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        int64_t t;
        r >> max_solutions >> max_depth >> memory_mb >> max_threads >> t;
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

}; // namespace algorithm
