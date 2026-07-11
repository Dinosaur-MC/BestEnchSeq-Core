#pragma once
#include <cstdint>

// ─── Minecraft platform edition ─────────────────────────────────────────────
enum class MCE : int8_t {
    None    = 0x00,
    Java    = 0x01,
    Bedrock = 0x02,
    All     = 0x03,
};

// ─── Forge configuration ────────────────────────────────────────────────────
struct ForgeConfig {
    bool ignore_penalty_cost = false;
    bool ignore_repair_cost  = false; // [reserved] future: skip repair cost in forge_into
    bool ignore_cost_cap     = false;
    MCE platform             = MCE::Java;
};
