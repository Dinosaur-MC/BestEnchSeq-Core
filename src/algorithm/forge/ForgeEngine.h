#pragma once
#include "IForgeEngine.h"

/// Default forge engine implementing vanilla Minecraft rules.
/// Implements the IForgeEngine interface; subclass for modded behavior.
class ForgeEngine : public IForgeEngine {
public:
    explicit ForgeEngine(bool ignore_penalty_cost = false,
                         bool ignore_cost_cap = false) noexcept
        : _ignore_penalty(ignore_penalty_cost)
        , _ignore_cap(ignore_cost_cap) {}

    int32_t forge_into(compact::Item& target, const compact::Item& sacrifice,
                       const compact::EnchReg& reg) const override;

    std::pair<compact::Item, int32_t> forge(const compact::Item& target,
                                             const compact::Item& sacrifice,
                                             const compact::EnchReg& reg) const override;

    bool is_forgeable(const compact::Item& a, const compact::Item& b) const noexcept override;

private:
    static int32_t _penalty_cost(int8_t ppn) noexcept;
    int32_t _apply_cap(int32_t raw) const noexcept;

    bool _ignore_penalty;
    bool _ignore_cap;
};
