#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <utility>

namespace compact {

/// Virtual forge engine interface for mod customization.
/// DefaultForgeEngine provides the vanilla Minecraft implementation;
/// subclass and override methods for modded behavior.
class IForgeEngine {
public:
    virtual ~IForgeEngine() = default;

    /// Forge @p sacrifice into @p target (modifies @p target in-place).
    /// Returns the forge cost in levels.
    virtual int32_t forge_into(Item& target, const Item& sacrifice,
                               const EnchReg& reg) const = 0;

    /// Non-mutating forge. Returns (result_item, cost).
    virtual std::pair<Item, int32_t> forge(const Item& target, const Item& sacrifice,
                                           const EnchReg& reg) const = 0;

    /// Check whether two items can be forged together.
    virtual bool is_forgeable(const Item& a, const Item& b) const noexcept = 0;
};

} // namespace compact
