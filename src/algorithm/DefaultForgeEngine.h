#pragma once
#include "IForgeEngine.h"

class DefaultForgeEngine : public IForgeEngine {
public:
    explicit DefaultForgeEngine(ForgeConfig config = {});
    ~DefaultForgeEngine() override = default;

    std::pair<ItemStack, int32_t> forge(
        const ItemStack& item_a, const ItemStack& item_b, bool updated = false
    ) const override;

    int32_t forge_into(
        ItemStack& item_a, const ItemStack& item_b, bool updated = false
    ) const override;

    bool is_forgeable(const ItemStack& a, const ItemStack& b) const noexcept override;
    const ForgeConfig& get_config() const noexcept override;

protected:
    // Sub-operations exposed as protected virtual methods; subclasses may selectively override
    virtual std::pair<EnchSet, int32_t> combine_enchantments(
        const EnchSet& base, const EnchSet& addition, bool is_book, bool updated) const;

    virtual int32_t calc_penalty_cost(int32_t penalty_a, int32_t penalty_b) const noexcept;
    virtual int32_t calc_durability(
        const EquipmentType* equipment, int32_t durability_a,
        int32_t durability_b, bool is_equip_b) const noexcept;

private:
    ForgeConfig _config;
    int32_t _apply_forge_cost_cap(int32_t raw_cost) const noexcept;
};
