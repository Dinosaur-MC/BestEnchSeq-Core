#pragma once
#include "types/Platform.h"

// ─── Forge configuration ────────────────────────────────────────────────────
struct ForgeConfig {
    bool ignore_penalty_cost = false;
    bool ignore_repair_cost  = false; // when true, skip equip+equip repair fee (+2)
    bool ignore_cost_cap     = false;
    MCE platform             = MCE::Java;
};
