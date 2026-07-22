#pragma once
#include "business-domain/types/Solution.h"
#include "business-domain/types/Item.h"

#include "common/io/json.h"

#include <string>
#include <vector>

class EnchantmentRegistry;
class EquipmentCategoryRegistry;

class OutputFormatter {
  public:
    static std::string format_verbose(
        const std::vector<Solution> &solutions,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg,
        const std::string &mode_name
    );

    static std::string format_compact(
        const std::vector<Solution> &solutions,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg,
        const std::string &mode_name
    );

    /// Clear the internal JSON deserialization equipment cache.
    /// Call between bulk parse_json calls to cap memory growth.
    static void clear_cache();

    static std::string format_json(
        const std::vector<Solution> &solutions,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg,
        const std::string &mode_name
    );

    static std::vector<Solution> parse_json(
        const std::string &json_str,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );

  private:
    static std::string describe_item_verbose(
        const Item &item,
        const EnchantmentRegistry &ench_reg
    );
    static std::string describe_item_compact(
        const Item &item,
        const EnchantmentRegistry &ench_reg
    );
    static std::string describe_ench_roman(
        const Ench &ench,
        const EnchantmentRegistry &ench_reg
    );
    static std::string mode_display_name(const std::string &mode);
    static std::string platform_to_display(MCE p);

    // JSON helpers
    static Json item_to_json(
        const Item &item,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );
    static Item item_from_json(
        const Json &j,
        std::vector<Equipment> &equipment_cache,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );
    static Json step_to_json(
        const Solution::EnchStep &step,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );
    static Solution::EnchStep step_from_json(
        const Json &j,
        std::vector<Equipment> &equipment_cache,
        const EnchantmentRegistry &ench_reg,
        const EquipmentCategoryRegistry &cat_reg
    );
};
