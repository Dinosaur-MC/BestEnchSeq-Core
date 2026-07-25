#pragma once
#include "domain/business/types/Solution.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/Profile.h"

#include "common/CommonTypes.h"
#include "common/io/json.h"

#include <string>
#include <vector>

class EnchantmentRegistry;
class EquipmentTagRegistry;

class OutputFormatter {
  public:
    static std::string format_verbose(
        const std::vector<Solution> &solutions,
        const Profile &profile,
        AlgorithmMode mode
    );

    static std::string format_compact(
        const std::vector<Solution> &solutions,
        const Profile &profile,
        AlgorithmMode mode
    );

    static std::string format_json(
        const std::vector<Solution> &solutions,
        const Profile &profile,
        AlgorithmMode mode
    );

    static std::vector<Solution> parse_json(
        const std::string &json_str,
        const Profile &profile
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
    static std::string mode_display_name(AlgorithmMode mode);
    static std::string platform_to_display(MCE p);

    // JSON helpers
    static Json item_to_json(
        const Item &item,
        const EnchantmentRegistry &ench_reg,
        const EquipmentTagRegistry &cat_reg
    );
    static Item item_from_json(
        const Json &j,
        std::vector<Equipment> &equipment_cache,
        const EnchantmentRegistry &ench_reg,
        const EquipmentTagRegistry &cat_reg
    );
    static Json step_to_json(
        const Solution::EnchStep &step,
        const EnchantmentRegistry &ench_reg,
        const EquipmentTagRegistry &cat_reg
    );
    static Solution::EnchStep step_from_json(
        const Json &j,
        std::vector<Equipment> &equipment_cache,
        const EnchantmentRegistry &ench_reg,
        const EquipmentTagRegistry &cat_reg
    );
};
