#pragma once
#include <cstddef>

// ─── Shared algorithm utilities ───
namespace AlgorithmUtils {

inline void hash_combine(size_t& seed, size_t v) noexcept {
    seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

} // namespace AlgorithmUtils
