#pragma once
#include "IForgeEngine.h"

/// Vanilla forge engine — implements Minecraft anvil mechanics.
///
/// ## Cost model (see docs/anvil-mechanics-reference.md)
///
///   Cost = C_ench + P_A + P_B + R_E + R_D
///          └─ forge_into()   └─ penalty_cost() (config-gated)
///
///   C_ench: Java (final_level × mult) or Bedrock (diff × mult),
///           plus +1 per conflict on Java only.
///   P_A+B:  (1 << ppn) - 1 each, gated by ignore_penalty_cost.
///   R_E:    rename, currently not implemented.
///   R_D:    repair, gated by ignore_repair_cost (reserved).
///
/// All sub-operations override IForgeEngine defaults with config checks.
/// Subclass to customize specific behaviors for modded rules.
namespace algorithm {
class ForgeEngine : public IForgeEngine {
public:
    explicit ForgeEngine(ForgeConfig cfg = {}) noexcept
        : _config(std::move(cfg)) {}

    // ── IForgeEngine configuration ────────────────────────────────────────────

    const ForgeConfig& get_config() const noexcept override { return _config; }
    void set_config(const ForgeConfig& cfg) noexcept override { _config = cfg; }

    // ── IForgeEngine core ─────────────────────────────────────────────────────

    /// Forge target with sacrifice (mutating).
    ///
    /// Matches the formal model in docs/anvil-mechanics-reference.md §1-3, §5-7:
    ///   Cost = P_A + P_B             (§2 penalty_cost, gated by ignore_penalty_cost)
    ///        + C_ench                 (§3 ench merge, §9 JE/BE dispatching)
    ///        + conflict_penalty       (§3 incompatibility, JE only)
    ///   Result.ppn = max(a,b) + 1     (§2 penalty update)
    int32_t forge_into(Item& target, const Item& sacrifice,
                       const EnchReg& reg) const override;

    /// Non-mutating forge: copy target, call forge_into, return {result, cost}.
    std::pair<Item, int32_t> forge(const Item& target,
                                             const Item& sacrifice,
                                             const EnchReg& reg) const override;

    /// Check whether (target + sacrifice) is a valid forge operation per §8.
    bool is_forgeable(const Item& a, const Item& b) const noexcept override;

    /// Pure forge: cost-free state mutation for simulate().
    void pure_forge_into(Item& target, const Item& sacrifice,
                          const EnchReg& reg) const noexcept override;

    // ── IForgeEngine sub-operations ───────────────────────────────────────────

    int32_t penalty_cost(int8_t ppn) const noexcept override;

    int32_t estimate_forge_cost(const Item& target,
                                 const Item& sacrifice,
                                 const EnchReg& reg) const noexcept override;

private:
    ForgeConfig _config;
};
} // namespace algorithm
