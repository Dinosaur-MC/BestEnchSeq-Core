#pragma once
#include "types/EnchSolution.h"
#include "types/ItemStack.h"
#include "types/common.h"

#include "io/json.h"

#include <string>
#include <vector>

class OutputFormatter {
  public:
    static std::string format_verbose(
        const std::vector<EnchSolution> &solutions,
        const std::string &mode_name
    );

    static std::string format_compact(
        const std::vector<EnchSolution> &solutions,
        const std::string &mode_name
    );

    static std::string format_json(
        const std::vector<EnchSolution> &solutions,
        const std::string &mode_name
    );

    static std::vector<EnchSolution> parse_json(const std::string &json_str);

  private:
    static std::string describe_item_verbose(const ItemStack &item);
    static std::string describe_item_compact(const ItemStack &item);
    static std::string describe_ench_roman(const Ench &ench);
    static std::string mode_display_name(const std::string &mode);
    static std::string platform_to_display(platform::MCE p);

    // JSON helpers
    static Json itemstack_to_json(const ItemStack &item);
    static ItemStack itemstack_from_json(const Json &j, std::vector<Equipment> &equipment_cache);
    static Json step_to_json(const EnchSolution::EnchStep &step);
    static EnchSolution::EnchStep step_from_json(const Json &j, std::vector<Equipment> &equipment_cache);
};
