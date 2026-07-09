#pragma once
#include "IForgeEngine.h"

/// Default forge engine implementing vanilla Minecraft rules.
class ForgeEngine : public IForgeEngine {
public:
    explicit ForgeEngine(bool ignore_penalty_cost = false,
                         bool ignore_cost_cap = false) noexcept
        : _ignore_penalty(ignore_penalty_cost)
        , _ignore_cap(ignore_cost_cap) {}

    // ── IForgeEngine core ─────────────────────────────────────────────────────

    int32_t forge_into(compact::Item& target, const compact::Item& sacrifice,
                       const compact::EnchReg& reg) const override;

    std::pair<compact::Item, int32_t> forge(const compact::Item& target,
                                             const compact::Item& sacrifice,
                                             const compact::EnchReg& reg) const override;

    bool is_forgeable(const compact::Item& a, const compact::Item& b) const noexcept override;

    // ── IForgeEngine sub-operations ───────────────────────────────────────────

    int32_t penalty_cost(int8_t ppn) const noexcept override;
    int32_t book_multiplier(int32_t equip_mult) const noexcept override;
    int32_t apply_cap(int32_t raw_cost) const noexcept override;

    int32_t estimate_forge_cost(const compact::Item& target,
                                 const compact::Item& sacrifice,
                                 const compact::EnchReg& reg) const noexcept override;

private:
    bool _ignore_penalty;
    bool _ignore_cap;
};
