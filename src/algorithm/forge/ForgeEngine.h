#pragma once
#include "IForgeEngine.h"

/// Default forge engine implementing vanilla Minecraft rules.
class ForgeEngine : public IForgeEngine {
public:
    explicit ForgeEngine(ForgeConfig cfg = {}) noexcept
        : _config(std::move(cfg)) {}

    // ── IForgeEngine configuration ────────────────────────────────────────────

    const ForgeConfig& get_config() const noexcept override { return _config; }
    void set_config(const ForgeConfig& cfg) noexcept override { _config = cfg; }

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
    ForgeConfig _config;
};
