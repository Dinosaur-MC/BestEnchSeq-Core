#pragma once
#include <cstddef>
#include <cstdint>

/// Compute 56-bit checksum over a byte range.
void compute_crc56(const uint8_t* data, size_t len, uint8_t crc[7]);
