#pragma once
#include <cstdint>

enum class MCE : int8_t {
    None    = 0x00,
    Java    = 0x01,
    Bedrock = 0x02,
    All     = 0x03,
};
