#include "OutputFormatter.h"
#include "domain/business/business.h"
#include "common/i18n/Language.h"

namespace {

// ---------------------------------------------------------------------------
// Roman numeral conversion (1 .. 10)
// ---------------------------------------------------------------------------
std::string to_roman(int level) {
    static const std::pair<int, const char *> romans[] = {
        {10, "X"},   {9, "IX"},   {8, "VIII"}, {7, "VII"},
        {6, "VI"},   {5, "V"},    {4, "IV"},   {3, "III"},
        {2, "II"},   {1, "I"},
    };
    std::string result;
    if (level > 10) return "enchantment.level." + std::to_string(level);
    for (const auto &pair : romans) {
        while (level >= pair.first) {
            result += pair.second;
            level -= pair.first;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Enchantment name helpers (with graceful fallback when EnchInfo is absent)
// ---------------------------------------------------------------------------
std::string ench_name_id(const NSID& id, const EnchantmentRegistry &ench_reg) {
    try {
        return ench_reg.at(id).id.str();
    } catch (const std::exception &) {
        return "ench_" + id.str();
    }
}

// ---------------------------------------------------------------------------
// JSON extraction helpers
// ---------------------------------------------------------------------------
int32_t json_int(const Json &j) {
    return static_cast<int32_t>(j.as_int());
}

std::string json_str(const Json &j) {
    return j.as_string();
}

// ---------------------------------------------------------------------------
// Enchantment summary for verbose header
// ---------------------------------------------------------------------------
std::string ench_summary_str(const EnchSet &enchs, const EnchantmentRegistry &ench_reg) {
    if (enchs.empty()) return {};
    std::string result;
    bool first = true;
    for (const auto &ench : enchs) {
        if (!first) result += tr("output.item.enchant_sep");
        first = false;
        result += ench_name_id(ench.id, ench_reg) + " " + to_roman(ench.level);
    }
    return result;
}

// ---------------------------------------------------------------------------
// EnchSet from JSON array (used during JSON deserialization; unknown enchants
// are silently skipped).
// ---------------------------------------------------------------------------
EnchSet enchset_from_json_array(const Json::Array &arr, const EnchantmentRegistry &ench_reg) {
    EnchSet result;
    for (const auto &elem : arr) {
        std::string eid = json_str(elem["id"]);
        int32_t level   = json_int(elem["level"]);
        NSID ench_nsid(eid);
        if (ench_reg.contains(ench_nsid)) {
            result.emplace(std::move(ench_nsid), eid, level);
        }
    }
    return result;
}

} // anonymous namespace

// ===========================================================================
// OutputFormatter public methods
// ===========================================================================

// ---------------------------------------------------------------------------
// describe_item_verbose
// ---------------------------------------------------------------------------
std::string OutputFormatter::describe_item_verbose(
    const Item &item, const EnchantmentRegistry &ench_reg
) {
    std::string result;

    // Build enchantment part "Ench X, Ench Y"
    std::string ench_part;
    if (!item.enchantments.empty()) {
        bool first = true;
        for (const auto &ench : item.enchantments) {
            if (!first) ench_part += ", ";
            first = false;
            ench_part += ench_name_id(ench.id, ench_reg) + " " + to_roman(ench.level);
        }
    }

    if (item.is_book()) {
        if (item.enchantments.empty()) {
            return item.id.str() + " " + tr("output.item.free");
        }
        return item.id.str() + "[" + ench_part + "]{ppn=" + std::to_string(item.prior_penalty) + "}";
    }

    // Equipment
    result = item.id.str();
    if (!ench_part.empty()) {
        result += "[" + ench_part + "]";
    }
    result += "{ppn=" + std::to_string(item.prior_penalty) +
              ",dur=" + std::to_string(item.durability) + "}";

    if (item.prior_penalty == 0 && item.enchantments.empty()) {
        result += " " + tr("output.item.free");
    }

    return result;
}

// ---------------------------------------------------------------------------
// describe_item_compact
// ---------------------------------------------------------------------------
std::string OutputFormatter::describe_item_compact(
    const Item &item, const EnchantmentRegistry &ench_reg
) {
    if (item.is_book()) {
        std::string result = "B;";
        bool first = true;
        for (const auto &ench : item.enchantments) {
            if (!first) result += ",";
            first = false;
            result += ench_name_id(ench.id, ench_reg) + ":" + std::to_string(ench.level);
        }
        result += ";P;" + std::to_string(item.prior_penalty);
        return result;
    }

    // Equipment
    std::string result = "E;" + item.id.str() + ";";
    bool first = true;
    for (const auto &ench : item.enchantments) {
        if (!first) result += ",";
        first = false;
        result += ench_name_id(ench.id, ench_reg) + ":" + std::to_string(ench.level);
    }
    result += ";P;" + std::to_string(item.prior_penalty);
    result += ";D;" + std::to_string(item.durability);
    return result;
}

// ---------------------------------------------------------------------------
// describe_ench_roman
// ---------------------------------------------------------------------------
std::string OutputFormatter::describe_ench_roman(
    const Ench &ench, const EnchantmentRegistry &ench_reg
) {
    return ench_name_id(ench.id, ench_reg) + " " + to_roman(ench.level);
}

// ---------------------------------------------------------------------------
// mode helpers
// ---------------------------------------------------------------------------
std::string OutputFormatter::mode_display_name(AlgorithmMode mode) {
    switch (mode) {
    case AlgorithmMode::direct:    return tr("output.mode.direct");
    case AlgorithmMode::inventory: return tr("output.mode.inventory");
    default:                       return tr("output.mode.unknown");
    }
}

static std::string mode_to_raw(AlgorithmMode mode) {
    switch (mode) {
    case AlgorithmMode::direct:    return "direct";
    case AlgorithmMode::inventory: return "inventory";
    default:                       return "unknown";
    }
}

// ---------------------------------------------------------------------------
// platform_to_display
// ---------------------------------------------------------------------------
std::string OutputFormatter::platform_to_display(MCE p) {
    switch (p) {
    case MCE::Java:    return tr("output.platform.java");
    case MCE::Bedrock: return tr("output.platform.bedrock");
    case MCE::All:     return tr("output.platform.all");
    default:                     return tr("output.platform.unknown");
    }
}

// ===========================================================================
// Format verbose
// ===========================================================================
std::string OutputFormatter::format_verbose(
    const std::vector<Solution> &solutions,
    const Profile &profile,
    AlgorithmMode mode
) {
    const auto &ench_reg = profile.ench();
    if (solutions.empty()) return {};

    std::string out;
    int32_t reference_cost = solutions[0].total_exp_level_cost;

    for (size_t i = 0; i < solutions.size(); ++i) {
        const auto &sol = solutions[i];

        // Separator between solutions (not before first)
        if (i > 0) {
            out += tr("output.verbose.separator") + "\n";
        }

        // Header line
        out += tr("output.verbose.separator") + "\n";
        out += "   " + tr("output.verbose.forge_plan");
        if (!sol.is_success) {
            out += " " + tr("output.verbose.infeasible");
        }
        if (!sol.original_ench.empty()) {
            out += " - " + ench_summary_str(sol.original_ench, ench_reg);
        }
        out += "\n" + tr("output.verbose.separator") + "\n";

        // Algorithm, mode, platform
        out += tr_fmt("output.verbose.mode", mode_display_name(mode)) + "\n";
        if (!sol.metadata.algorithm_name.empty())
            out += "Algorithm: " + sol.metadata.algorithm_name + "\n";
        out += tr_fmt("output.verbose.platform", platform_to_display(sol.platform)) + "\n";

        // Rank
        out += tr_fmt("output.verbose.rank", i + 1, solutions.size());
        if (i > 0 && sol.total_exp_level_cost == reference_cost) {
            out += " " + tr("output.verbose.same_cost");
        }
        out += "\n\n";

        // Total cost
        out += tr_fmt("output.verbose.total_cost", sol.total_exp_level_cost, sol.total_exp_cost) + "\n";

        // Peak step
        if (sol.is_feasible()) {
            out += tr_fmt("output.verbose.peak_step", sol.max_cost_step_index + 1,
                          sol.get_peak_level_cost(), sol.get_peak_exp_cost()) + "\n";
            // Warning for Too Expensive!
            if (sol.get_peak_level_cost() >= 39) {
                out += tr("output.verbose.too_expensive") + "\n";
            }
        }

        out += "\n" + tr("output.verbose.input_section") + "\n";
        out += tr_fmt("output.verbose.target_item", describe_item_verbose(sol.target_item, ench_reg)) + "\n";
        if (!sol.available_items.empty()) {
            out += tr("output.verbose.available_items") + "\n";
            for (const auto &item : sol.available_items) {
                out += "    - " + describe_item_verbose(item, ench_reg) + "\n";
            }
        }

        out += tr("output.verbose.step_separator") + "\n";
        for (size_t j = 0; j < sol.steps.size(); ++j) {
            const auto &step = sol.steps[j];

            out += tr_fmt("output.verbose.step_prefix", j + 1) +
                   describe_item_verbose(step.item_a, ench_reg) + tr("output.verbose.plus") +
                   describe_item_verbose(step.item_b, ench_reg) + "\n";
            out += tr_fmt("output.verbose.step_cost", step.exp_level_cost, step.exp_cost) + "\n";
        }
        out += tr("output.verbose.step_separator") + "\n";

        // Final item
        if (sol.final_item.has_value()) {
            out += "\n" + tr_fmt("output.verbose.final_item",
                describe_item_verbose(*sol.final_item, ench_reg)) + "\n";
        }
    }

    return out;
}

// ===========================================================================
// Format compact
// ===========================================================================
std::string OutputFormatter::format_compact(
    const std::vector<Solution> &solutions,
    const Profile &profile,
    AlgorithmMode mode
) {
    const auto &ench_reg = profile.ench();
    std::string out;
    out += "#MODE=" + mode_to_raw(mode) + "\n";
    if (solutions.empty()) return out;

    out += "#PLATFORM=" + platform_to_display(solutions[0].platform) + "\n";
    out += "#SOLUTIONS=" + std::to_string(solutions.size()) + "\n";

    for (size_t i = 0; i < solutions.size(); ++i) {
        const auto &sol = solutions[i];
        if (i > 0) {
            out += "===\n";
        }
        if (!sol.is_feasible()) continue;

        for (const auto &step : sol.steps) {
            out += std::to_string(i + 1) + "|" +          // solution rank
                   describe_item_compact(step.item_a, ench_reg) + "|" +
                   describe_item_compact(step.item_b, ench_reg) + "|" +
                   "|" +
                   std::to_string(step.exp_level_cost) + "|" +
                   std::to_string(step.exp_cost) + "\n";
        }
    }

    return out;
}

// ===========================================================================
// Format JSON
// ===========================================================================
std::string OutputFormatter::format_json(
    const std::vector<Solution> &solutions,
    const Profile &profile,
    AlgorithmMode mode
) {
    const auto &ench_reg = profile.ench();
    const auto &cat_reg = profile.tags();
    Json::Object root;
    root["schema_version"] = Json("1.0");
    root["mode"]           = Json(mode_to_raw(mode));

    Json::Array sol_arr;
    for (size_t si = 0; si < solutions.size(); ++si) {
        const auto &sol = solutions[si];
        Json::Object s;
        s["rank"] = Json(static_cast<int32_t>(si + 1));

        // Platform
        switch (sol.platform) {
        case MCE::Java:    s["platform"] = Json("Java");    break;
        case MCE::Bedrock: s["platform"] = Json("Bedrock"); break;
        case MCE::All:     s["platform"] = Json("All");     break;
        default:                     s["platform"] = Json("None");    break;
        }

        // Original enchantments
        Json::Array orig_arr;
        for (const auto &ench : sol.original_ench) {
            Json::Object eo;
            eo["id"]    = Json(ench_name_id(ench.id, ench_reg));
            eo["level"] = Json(ench.level);
            orig_arr.push_back(Json(eo));
        }
        s["original_ench"] = Json(orig_arr);

        // Target item
        s["target_item"] = item_to_json(sol.target_item, ench_reg, cat_reg);

        // Available items
        Json::Array avail_arr;
        for (const auto &item : sol.available_items) {
            avail_arr.push_back(item_to_json(item, ench_reg, cat_reg));
        }
        s["available_items"] = Json(avail_arr);

        // Steps
        Json::Array steps_arr;
        for (const auto &step : sol.steps) {
            steps_arr.push_back(step_to_json(step, ench_reg, cat_reg));
        }
        s["steps"] = Json(steps_arr);

        // Summary
        s["total_exp_level_cost"] = Json(sol.total_exp_level_cost);
        s["total_exp_cost"]       = Json(sol.total_exp_cost);
        s["peak_level_cost"]      = Json(sol.get_peak_level_cost());
        s["peak_exp_cost"]        = Json(sol.get_peak_exp_cost());
        s["max_cost_step_index"]  = Json(static_cast<int64_t>(sol.max_cost_step_index));
        s["is_success"]           = Json(sol.is_success);

        // Metadata
        Json::Object meta;
        meta["algorithm_name"]   = Json(sol.metadata.algorithm_name);
        meta["algorithm_version"] = Json(sol.metadata.algorithm_version);
        meta["created_at"]       = Json(static_cast<int64_t>(sol.metadata.created_at.time_since_epoch().count()));
        meta["computation_time"] = Json(static_cast<int64_t>(sol.metadata.computation_time.count()));
        s["metadata"]            = Json(meta);

        sol_arr.push_back(Json(s));
    }
    root["solutions"] = Json(sol_arr);

    return Json(root).to_string(Json::Pretty);
}

// ===========================================================================
// Parse JSON
// ===========================================================================
std::vector<Solution> OutputFormatter::parse_json(
    const std::string &input,
    const Profile &profile
) {
    const auto &ench_reg = profile.ench();
    const auto &cat_reg = profile.tags();

    std::vector<Equipment> equipment_cache;

    Json root = Json::parse(input);

    // Solutions array
    if (!root.has("solutions")) {
        return {};
    }
    Json::Array sol_arr = root["solutions"].as_array();

    std::vector<Solution> results;
    results.reserve(sol_arr.size());

    for (const auto &sol_json : sol_arr) {
        // Platform
        std::string plat_str = json_str(sol_json["platform"]);
        MCE plat = MCE::None;
        if (plat_str == "Java")       plat = MCE::Java;
        else if (plat_str == "Bedrock") plat = MCE::Bedrock;
        else if (plat_str == "All")   plat = MCE::All;

        // Original enchantments
        EnchSet orig_ench = enchset_from_json_array(
            sol_json["original_ench"].as_array(), ench_reg
        );

        // Target item
        Item target_item = item_from_json(sol_json["target_item"], equipment_cache, ench_reg, cat_reg);

        // Available items
        ItemCollection avail_items;
        if (sol_json.has("available_items")) {
            for (const auto &avail_j : sol_json["available_items"].as_array()) {
                avail_items.push_back(item_from_json(avail_j, equipment_cache, ench_reg, cat_reg));
            }
        }

        // Steps
        EnchStepList steps;
        if (sol_json.has("steps")) {
            for (const auto &step_j : sol_json["steps"].as_array()) {
                steps.push_back(step_from_json(step_j, equipment_cache, ench_reg, cat_reg));
            }
        }

        // Metadata
        Solution::MetaData meta;
        if (sol_json.has("metadata")) {
            Json meta_val = sol_json["metadata"];
            if (meta_val.has("algorithm_name"))
                meta.algorithm_name = meta_val["algorithm_name"].as<std::string>();
            if (meta_val.has("algorithm_version"))
                meta.algorithm_version = meta_val["algorithm_version"].as<std::string>();
            if (meta_val.has("created_at"))
                meta.created_at = std::chrono::system_clock::time_point{std::chrono::seconds(meta_val["created_at"].as<int64_t>())};
            if (meta_val.has("computation_time"))
                meta.computation_time = std::chrono::milliseconds(meta_val["computation_time"].as<int64_t>());
        }

        // is_success
        bool is_success = true;
        if (sol_json.has("is_success")) {
            is_success = sol_json["is_success"].as<bool>();
        }

        // Build solution via make() so costs are recomputed consistently
        results.push_back(Solution::make(plat, orig_ench, target_item, avail_items, steps, is_success, meta));
    }

    return results;
}

// ===========================================================================
// JSON helpers
// ===========================================================================

// ---------------------------------------------------------------------------
// Item_to_json
// ---------------------------------------------------------------------------
Json OutputFormatter::item_to_json(
    const Item &item,
    const EnchantmentRegistry &ench_reg,
    const EquipmentTagRegistry &cat_reg
) {
    Json::Object obj;

    // Equipment
    if (!item.is_book()) {
        Json::Object eq;
        eq["id"]             = Json(item.id.str());
        eq["category"]       = Json("unknown"); // temp
        eq["name"]           = Json(item.id.str());
        eq["max_durability"] = Json(0);
        obj["equipment"]     = Json(eq);
        obj["is_book"]       = Json(false);
    } else {
        obj["equipment"] = Json::null();
        obj["is_book"]   = Json(true);
    }

    // Enchantments
    Json::Array ench_arr;
    for (const auto &ench : item.enchantments) {
        Json::Object eo;
        eo["id"]    = Json(ench_name_id(ench.id, ench_reg));
        eo["level"] = Json(ench.level);
        ench_arr.push_back(Json(eo));
    }
    obj["enchantments"] = Json(ench_arr);

    obj["prior_penalty"] = Json(item.prior_penalty);
    obj["durability"]    = Json(item.durability);

    return Json(obj);
}

// ---------------------------------------------------------------------------
// Item_from_json
// ---------------------------------------------------------------------------
Item OutputFormatter::item_from_json(
    const Json &j,
    std::vector<Equipment> &equipment_cache,
    const EnchantmentRegistry &ench_reg,
    const EquipmentTagRegistry &cat_reg
) {
    // Equipment (may be null for books)
    const Equipment *eq_ptr = nullptr;
    if (j.has("equipment")) {
        if (j["equipment"].is_null()) {
            eq_ptr = nullptr;
        } else {
            std::string id     = json_str(j["equipment"]["id"]);
            std::string cat    = json_str(j["equipment"]["category"]);
            std::string name   = json_str(j["equipment"]["name"]);
            int32_t max_dur    = 0;
            if (j["equipment"].has("max_durability")) {
                max_dur = json_int(j["equipment"]["max_durability"]);
            }

            equipment_cache.emplace_back(Equipment{
                NSID(id), name, NSID(), max_dur
            });
            eq_ptr = &equipment_cache.back();
        }
    }

    // Enchantments
    EnchSet ench_set;
    if (j.has("enchantments")) {
        ench_set = enchset_from_json_array(
            j["enchantments"].as_array(), ench_reg
        );
    }

    // Prior penalty
    int32_t prior_penalty = 0;
    if (j.has("prior_penalty")) {
        prior_penalty = json_int(j["prior_penalty"]);
    }

    // Durability
    int32_t durability = -1;
    if (j.has("durability")) {
        durability = json_int(j["durability"]);
    }

    if (eq_ptr)
        return Item(eq_ptr->id, ench_set, prior_penalty, durability);
    return Item(NSID("minecraft:enchanted_book"), ench_set, prior_penalty);
}

// ---------------------------------------------------------------------------
// step_to_json
// ---------------------------------------------------------------------------
Json OutputFormatter::step_to_json(
    const Solution::EnchStep &step,
    const EnchantmentRegistry &ench_reg,
    const EquipmentTagRegistry &cat_reg
) {
    Json::Object obj;
    obj["item_a"]         = item_to_json(step.item_a, ench_reg, cat_reg);
    obj["item_b"]         = item_to_json(step.item_b, ench_reg, cat_reg);
    obj["exp_level_cost"] = Json(step.exp_level_cost);
    obj["exp_cost"]       = Json(step.exp_cost);
    return Json(obj);
}

// ---------------------------------------------------------------------------
// step_from_json
// ---------------------------------------------------------------------------
Solution::EnchStep OutputFormatter::step_from_json(
    const Json &j,
    std::vector<Equipment> &equipment_cache,
    const EnchantmentRegistry &ench_reg,
    const EquipmentTagRegistry &cat_reg
) {
    Solution::EnchStep step;
    step.item_a         = item_from_json(j["item_a"], equipment_cache, ench_reg, cat_reg);
    step.item_b         = item_from_json(j["item_b"], equipment_cache, ench_reg, cat_reg);
    step.exp_level_cost = json_int(j["exp_level_cost"]);
    step.exp_cost       = json_int(j["exp_cost"]);
    return step;
}
