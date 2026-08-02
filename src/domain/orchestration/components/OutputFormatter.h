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
class TagRegistry;
class EquipmentRegistry;

class OutputFormatter {
  public:
    /// When set, enchantment/item display names include the NSID in
    /// parentheses: ``锋利 (minecraft:sharpness)`` instead of ``锋利``.
    static void set_show_nsid(bool show) noexcept { _show_nsid = show; }
    static bool show_nsid() noexcept { return _show_nsid; }

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

    /// Build the root metadata object shared by the CLI `--format json` and the
    /// C ABI `besq_solve` so the two outputs cannot drift.
    ///
    /// Root schema (v1.0)::
    ///   {
    ///     "schema_version": "1.0",
    ///     "mode": "direct" | "inventory",
    ///     "success": true|false,
    ///     "algorithm": "<algorithm name>",
    ///     "computation_time_ms": <int64 ms>,
    ///     "solutions": [ ... ]
    ///   }
    static Json build_json_root(
        AlgorithmMode mode,
        bool success,
        const std::string &algorithm,
        int64_t computation_time_ms
    );

    /// Format solutions as JSON. The root metadata fields (`success`,
    /// `algorithm`, `computation_time_ms`) mirror the C ABI root; when the
    /// optional arguments are omitted the algorithm name / time are derived
    /// from the first solution's metadata.
    static std::string format_json(
        const std::vector<Solution> &solutions,
        const Profile &profile,
        AlgorithmMode mode,
        bool success = true,
        const std::string &algorithm = {},
        int64_t computation_time_ms = 0
    );

    static std::vector<Solution> parse_json(
        const std::string &json_str,
        const Profile &profile
    );

  private:
    static bool _show_nsid;
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
        const TagRegistry &cat_reg,
        const EquipmentRegistry &eq_reg
    );
    static Item item_from_json(
        const Json &j,
        std::vector<Equipment> &equipment_cache,
        const EnchantmentRegistry &ench_reg,
        const TagRegistry &cat_reg
    );
    static Json step_to_json(
        const Solution::EnchStep &step,
        const EnchantmentRegistry &ench_reg,
        const TagRegistry &cat_reg,
        const EquipmentRegistry &eq_reg
    );
    static Solution::EnchStep step_from_json(
        const Json &j,
        std::vector<Equipment> &equipment_cache,
        const EnchantmentRegistry &ench_reg,
        const TagRegistry &cat_reg
    );
};
