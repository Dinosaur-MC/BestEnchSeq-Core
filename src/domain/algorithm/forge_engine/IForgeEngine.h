#pragma once
#include "common/utils/bit_iterator.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Item.h"
#include <cstdint>
#include <utility>

namespace algorithm {

// ─── Virtual forge engine interface ─────────────────────────────────────────
class IForgeEngine {
  public:
    virtual ~IForgeEngine() = default;

    virtual const ForgeConfig &get_config() const noexcept   = 0;
    virtual void set_config(const ForgeConfig &cfg) noexcept = 0;

    virtual int32_t forge_into(Item &target, const Item &sacrifice, const EnchReg &reg) const = 0;

    virtual std::pair<Item, int32_t>
    forge(const Item &target, const Item &sacrifice, const EnchReg &reg) const = 0;

    virtual bool is_forgeable(const Item &a, const Item &b) const noexcept = 0;

    /// Pure forge: forge target with sacrifice without computing costs.
    /// Modifies target state (enchants, ppn, durability) but skips all cost
    /// arithmetic.  Default implementation calls forge_into() and discards
    /// the cost; subclasses may override with a cost-free path for speed.
    virtual void pure_forge_into(Item &target, const Item &sacrifice, const EnchReg &reg) const {
        forge_into(target, sacrifice, reg);
    }

    // ── Forge sub-operations (default vanilla implementations) ────────────
    virtual int32_t penalty_cost(int8_t ppn) const noexcept {
        // Cap at 30 to avoid UB from shifting into the sign bit (1<<31).
        // Vanilla ppn rarely exceeds 6; modded data or unbounded sequences
        // could hit higher values for out-of-range inputs.
        if (ppn < 0 || ppn > 30)
            return INT32_MAX;
        return (1 << ppn) - 1;
    }
    /// Quick cost estimate for pair ordering and pre-pruning only.
    /// Does NOT apply cost cap (39 max) — intentional: overestimate is safe
    /// for ordering heuristics and upper-bound pruning.  Do not use as an
    /// admissible lower bound without capping first.
    virtual int32_t
    estimate_forge_cost(const Item &target, const Item &sacrifice, const EnchReg &reg) const noexcept {
        int32_t cost     = penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn);
        bool sac_is_book = (sacrifice.type == ItemType::Book);
        for (sbit_iterator<EnchSet::mask_type, uint8_t> it(sacrifice.enchs.get_mask()); it; ++it) {
            int32_t mult = sac_is_book ? reg[*it].mul_b : reg[*it].mul;
            cost += sacrifice.enchs[*it] * mult;
        }
        return cost;
    }
};

} // namespace algorithm
