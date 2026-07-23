#include "CompactSerializer.h"
#include <cstring>

// ── CRC-56 helper ───────────────────────────────────────────────────────
//
// Simple 56-bit checksum: for each byte, XOR into one accumulator and
// ADD into another, rotating across 7 lanes.  Adequate for accidental
// corruption detection within checkpoint files.

void compute_crc56(const uint8_t* data, size_t len, uint8_t crc[7]) {
    std::memset(crc, 0, 7);
    for (size_t i = 0; i < len; ++i) {
        size_t lane = i % 7;
        crc[lane]       = static_cast<uint8_t>(crc[lane] ^ data[i]);
        crc[(lane+1)%7] = static_cast<uint8_t>(crc[(lane+1)%7] + data[i]);
    }
}
