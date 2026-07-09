#pragma once
#include "../../BESQTypes.h"
#include <cstdint>
#include <utility>

struct ForgeConfig {
    bool ignore_penalty_cost = false;
    bool ignore_repair_cost  = false;
    bool ignore_cost_cap     = false;
};

class IForgeEngine {
public:
    virtual ~IForgeEngine() = default;

    virtual std::pair<ItemStack, int32_t> forge(
        const ItemStack& item_a,
        const ItemStack& item_b,
        bool updated = false
    ) const = 0;

    // In-place forge: modifies item_a directly, returns step cost.
    // Avoids the ItemStack copy that forge() incurs — the result is
    // written into item_a, and the step cost is returned as int32_t.
    virtual int32_t forge_into(
        ItemStack& item_a,
        const ItemStack& item_b,
        bool updated = false
    ) const = 0;

    virtual bool is_forgeable(const ItemStack& a, const ItemStack& b) const noexcept = 0;
    virtual const ForgeConfig& get_config() const noexcept = 0;
};
