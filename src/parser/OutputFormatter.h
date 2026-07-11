#pragma once
#include "types/EnchSolution.h"
#include "types/ItemStack.h"
#include "types/ForgeConfig.h"

#include "io/json.h"

#include <string>
#include <vector>

class EnchantmentRegistry;
class EquipmentCategoryRegistry;

class OutputFormatter {
  public:
    static std::string format_verbose(
        const std::vector<EnchSolution> &solutions,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg,
        const std::string &mode_name
    );

    static std::string format_compact(
        const std::vector<EnchSolution> &solutions,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg,
        const std::string &mode_name
    );

    static std::string format_json(
        const std::vector<EnchSolution> &solutions,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg,
        const std::string &mode_name
    );

    static std::vector<EnchSolution> parse_json(
        const std::string &json_str,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );

  private:
    static std::string describe_item_verbose(
        const ItemStack &item,
        const EnchantmentRegistry &ench_reg
    );
    static std::string describe_item_compact(
        const ItemStack &item,
        const EnchantmentRegistry &ench_reg
    );
    static std::string describe_ench_roman(
        const Ench &ench,
        const EnchantmentRegistry &ench_reg
    );
    static std::string mode_display_name(const std::string &mode);
    static std::string platform_to_display(MCE p);

    // JSON helpers
    static Json itemstack_to_json(
        const ItemStack &item,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );
    static ItemStack itemstack_from_json(
        const Json &j,
        std::vector<Equipment> &equipment_cache,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );
    static Json step_to_json(
        const EnchSolution::EnchStep &step,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );
    static EnchSolution::EnchStep step_from_json(
        const Json &j,
        std::vector<Equipment> &equipment_cache,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );
};
