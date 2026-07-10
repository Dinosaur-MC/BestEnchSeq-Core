#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include "types/ForgeConfig.h"
#include <algorithm>
#include <cstdint>
#include <utility>

// ─── Virtual forge engine interface ─────────────────────────────────────────
class IForgeEngine {
public:
    virtual ~IForgeEngine() = default;

    virtual const ForgeConfig& get_config() const noexcept = 0;
    virtual void set_config(const ForgeConfig& cfg) noexcept = 0;

    virtual int32_t forge_into(compact::Item& target, const compact::Item& sacrifice,
                               const compact::EnchReg& reg) const = 0;

    virtual std::pair<compact::Item, int32_t> forge(const compact::Item& target,
                                                     const compact::Item& sacrifice,
                                                     const compact::EnchReg& reg) const = 0;

    virtual bool is_forgeable(const compact::Item& a, const compact::Item& b) const noexcept = 0;

    // ── Forge sub-operations (default vanilla implementations) ────────────
    virtual int32_t penalty_cost(int8_t ppn) const noexcept {
        return (1 << ppn) - 1;
    }
    virtual int32_t book_multiplier(int32_t equip_mult) const noexcept {
        return std::max(1, equip_mult >> 1);
    }
    virtual int32_t apply_cap(int32_t raw_cost) const noexcept {
        return raw_cost > 39 ? 39 : raw_cost;
    }
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
