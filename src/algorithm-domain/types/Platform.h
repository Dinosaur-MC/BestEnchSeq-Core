#pragma once
#include <cstdint>

namespace algorithm {

// ─── Minecraft platform edition ─────────────────────────────────────────────
enum MCE : int8_t {
    None    = 0x00,
    Java    = 0x01,
    Bedrock = 0x02,
    All     = 0x03,
};

} // namespace algorithm::platform
