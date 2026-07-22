#pragma once
#include <cstddef>

inline constexpr std::size_t kHashPhi = (sizeof(std::size_t) == 8) ? 0x9e3779b97f4a7c15ULL // 64 位 φ⁻¹
                                                                   : 0x9e3779b9UL;         // 32 位 φ⁻¹

/// Combine a hash value `seed` with another value `v`.
/// Uses the boost::hash_combine algorithm (0x9e3779b9 golden ratio).
/// Call repeatedly to accumulate a hash from multiple fields:
///   size_t h = 0;
///   hash_combine(h, field1);
///   hash_combine(h, field2);
inline void hash_combine(size_t &seed, size_t v) noexcept {
    seed ^= v + kHashPhi + (seed << 6) + (seed >> 2);
}
