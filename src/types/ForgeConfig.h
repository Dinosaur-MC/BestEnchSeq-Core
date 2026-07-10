#pragma once
#include <cstdint>

// ─── Minecraft platform edition ─────────────────────────────────────────────
enum class MCE : int8_t {
    None    = 0x00,
    Java    = 0x01,
    Bedrock = 0x02,
    All     = 0x03,
};

inline MCE& active_platform_ref() {
    static MCE active = MCE::Java;
    return active;
}
inline MCE  get_active_platform()    { return active_platform_ref(); }
inline void set_active_platform(MCE t) { active_platform_ref() = t; }

// ─── Forge configuration ────────────────────────────────────────────────────
struct ForgeConfig {
    bool ignore_penalty_cost = false;
    bool ignore_repair_cost  = false;
    bool ignore_cost_cap     = false;
    MCE platform             = MCE::Java;
};
