#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <utility>

/// Virtual forge engine interface for mod customization.
/// Subclass and override methods for modded behavior.
/// All forge operations use compact types directly.
class IForgeEngine {
public:
    virtual ~IForgeEngine() = default;

    /// Forge @p sacrifice into @p target (modifies @p target in-place).
    /// Returns the forge cost in levels.
    virtual int32_t forge_into(compact::Item& target, const compact::Item& sacrifice,
                               const compact::EnchReg& reg) const = 0;

    /// Non-mutating forge. Returns (result_item, cost).
    virtual std::pair<compact::Item, int32_t> forge(const compact::Item& target,
                                                     const compact::Item& sacrifice,
                                                     const compact::EnchReg& reg) const = 0;

    /// Check whether two items can be forged together.
    virtual bool is_forgeable(const compact::Item& a, const compact::Item& b) const noexcept = 0;
};
