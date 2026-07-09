#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include "types/common.h"
#include <algorithm>
#include <cstdint>
#include <utility>

/// Configuration for a single forge engine instance.
/// Defaults match vanilla Java Edition behavior.
struct ForgeConfig {
    bool ignore_penalty_cost = false;
    bool ignore_repair_cost  = false;
    bool ignore_cost_cap     = false;
    platform::MCE platform   = platform::MCE::Java;
};

/// Virtual forge engine interface for mod customization.
/// All forge sub-operations have default vanilla implementations;
/// override only the methods your mod needs to change.
class IForgeEngine {
public:
    virtual ~IForgeEngine() = default;

    // ── Configuration ────────────────────────────────────────────────────────

    /// Return the current forge configuration.
    virtual const ForgeConfig& get_config() const noexcept = 0;

    /// Replace the current forge configuration.
    virtual void set_config(const ForgeConfig& cfg) noexcept = 0;

    // ── Core forge operations ────────────────────────────────────────────────

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

    // ── Forge sub-operations (default vanilla implementations) ────────────────

    /// Compute prior-work penalty cost for a given penalty count.
    /// Vanilla: (1 << ppn) - 1
    virtual int32_t penalty_cost(int8_t ppn) const noexcept {
        return (1 << ppn) - 1;
    }

    /// Compute book multiplier from equipment multiplier.
    /// Vanilla: max(1, equip_mult >> 1)
    virtual int32_t book_multiplier(int32_t equip_mult) const noexcept {
        return std::max(1, equip_mult >> 1);
    }

    /// Apply the per-operation cost cap (default: 39 levels).
    /// Return raw_cost unchanged to disable the cap.
    virtual int32_t apply_cap(int32_t raw_cost) const noexcept {
        return raw_cost > 39 ? 39 : raw_cost;
    }

    /// Estimate total forge cost without mutating items.
    /// Used by algorithms for pair ordering and heuristic computation.
    /// Calls penalty_cost() and book_multiplier() — overrides to those
    /// automatically propagate here.
    virtual int32_t estimate_forge_cost(const compact::Item& target,
                                         const compact::Item& sacrifice,
                                         const compact::EnchReg& reg) const noexcept {
        int32_t cost = penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn);
        bool sac_is_book = (sacrifice.type == compact::ItemType::Book);
        for (const auto& e : sacrifice.enchs) {
            int32_t mult = sac_is_book
                ? book_multiplier(reg.get_multiplier(e.id))
                : reg.get_multiplier(e.id);
            cost += e.level * mult;
        }
        return cost;
    }
};
