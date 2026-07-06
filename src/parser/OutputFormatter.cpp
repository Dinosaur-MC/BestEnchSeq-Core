#include "parser/OutputFormatter.h"
#include "registries/EnchantmentRegistry.h"
#include "types/EnchInfo.h"
#include "types/EnchSet.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace {

// ---------------------------------------------------------------------------
// Equipment cache for JSON deserialization (cleared on each parse_json call)
// ---------------------------------------------------------------------------
std::vector<EquipmentType> _json_eq_cache;

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
std::string ench_name_id(int32_t id) {
    try {
        return EnchantmentRegistry::get_instance().get(id).name_id;
    } catch (const std::exception &) {
        return "ench_" + std::to_string(id);
    }
}

std::string ench_display_name(int32_t id) {
    try {
        return EnchantmentRegistry::get_instance().get(id).name;
    } catch (const std::exception &) {
        return "ench_" + std::to_string(id);
    }
}

// ---------------------------------------------------------------------------
// JSON extraction helpers
// ---------------------------------------------------------------------------
int32_t json_int(const Json &j) {
    Json::Value val = j.get_value();
    const auto &num = std::get<Json::Number>(val);
    if (std::holds_alternative<int32_t>(num)) return std::get<int32_t>(num);
    if (std::holds_alternative<int64_t>(num)) return static_cast<int32_t>(std::get<int64_t>(num));
    return static_cast<int32_t>(std::get<double>(num));
}

int64_t json_int64(const Json &j) {
    Json::Value val = j.get_value();
    const auto &num = std::get<Json::Number>(val);
    if (std::holds_alternative<int64_t>(num)) return std::get<int64_t>(num);
    if (std::holds_alternative<int32_t>(num)) return std::get<int32_t>(num);
    return static_cast<int64_t>(std::get<double>(num));
}

std::string json_str(const Json &j) {
    return std::get<Json::String>(j.get_value());
}

// ---------------------------------------------------------------------------
// Enchantment summary for verbose header
// ---------------------------------------------------------------------------
std::string ench_summary_str(const EnchSet &enchs) {
    if (enchs.empty()) return {};
    std::string result;
    bool first = true;
    for (const auto &ench : enchs) {
        if (!first) result += " + ";
        first = false;
        result += ench_name_id(ench.id) + " " + to_roman(ench.level);
    }
    return result;
}

// ---------------------------------------------------------------------------
// EnchSet from JSON array (used during JSON deserialization; unknown enchants
// are silently skipped).
// ---------------------------------------------------------------------------
EnchSet enchset_from_json_array(const Json::Array &arr) {
    EnchSet result;
    for (const auto &elem : arr) {
        Json::Value elem_val = elem.get_value();
        const Json::Object &obj = std::get<Json::Object>(elem_val);
        std::string eid = json_str(obj.at("id"));
        int32_t level   = json_int(obj.at("level"));
        int32_t id      = EnchantmentRegistry::get_instance().get_id(eid);
        if (id >= 0) {
            result.emplace(id, level);
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
std::string OutputFormatter::describe_item_verbose(const ItemStack &item) {
    std::string result;

    if (item.is_book() || item.equipment == nullptr) {
        result = "附魔书(";
        if (item.enchantments.empty()) {
            result += "无魔咒";
        } else {
            bool first = true;
            for (const auto &ench : item.enchantments) {
                if (!first) result += ", ";
                first = false;
                result += ench_name_id(ench.id) + " " + to_roman(ench.level);
            }
        }
        result += ")";
    } else {
        // Equipment
        result += item.equipment->name + "(";
        bool first = true;
        for (const auto &ench : item.enchantments) {
            if (!first) result += ", ";
            first = false;
            result += ench_name_id(ench.id) + " " + to_roman(ench.level);
        }
        result += ")";
        result += "[铁砧惩罚 x" + std::to_string(item.prior_penalty) + "]";
        result += "[耐久 " + std::to_string(item.durability) + "]";
    }

    if (item.prior_penalty == 0 && item.enchantments.empty()) {
        result += "(免费)";
    }

    return result;
}

// ---------------------------------------------------------------------------
// describe_item_compact
// ---------------------------------------------------------------------------
std::string OutputFormatter::describe_item_compact(const ItemStack &item) {
    if (item.is_book() || item.equipment == nullptr) {
        std::string result = "B;";
        bool first = true;
        for (const auto &ench : item.enchantments) {
            if (!first) result += ",";
            first = false;
            result += ench_name_id(ench.id) + ":" + std::to_string(ench.level);
        }
        result += ";P;" + std::to_string(item.prior_penalty);
        return result;
    }

    // Equipment
    std::string result = "E;" + item.equipment->id + ";";
    bool first = true;
    for (const auto &ench : item.enchantments) {
        if (!first) result += ",";
        first = false;
        result += ench_name_id(ench.id) + ":" + std::to_string(ench.level);
    }
    result += ";P;" + std::to_string(item.prior_penalty);
    result += ";D;" + std::to_string(item.durability);
    return result;
}

// ---------------------------------------------------------------------------
// describe_ench_roman
// ---------------------------------------------------------------------------
std::string OutputFormatter::describe_ench_roman(const Ench &ench) {
    return ench_name_id(ench.id) + " " + to_roman(ench.level);
}

// ---------------------------------------------------------------------------
// mode_display_name
// ---------------------------------------------------------------------------
std::string OutputFormatter::mode_display_name(const std::string &mode) {
    if (mode == "direct")    return "简单锻造";
    if (mode == "inventory") return "库存锻造";
    // Fallback: return the mode string itself
    return mode;
}

// ---------------------------------------------------------------------------
// platform_to_display
// ---------------------------------------------------------------------------
std::string OutputFormatter::platform_to_display(platform::MCE p) {
    switch (p) {
    case platform::MCE::Java:    return "Java版";
    case platform::MCE::Bedrock: return "Bedrock版";
    case platform::MCE::All:     return "通用";
    default:                     return "未知";
    }
}

// ===========================================================================
// Format verbose
// ===========================================================================
std::string OutputFormatter::format_verbose(
    const std::vector<EnchSolution> &solutions,
    const std::string &mode_name
) {
    if (solutions.empty()) return {};

    std::string out;
    int32_t reference_cost = solutions[0].total_exp_level_cost;

    for (size_t i = 0; i < solutions.size(); ++i) {
        const auto &sol = solutions[i];

        // Separator between solutions (not before first)
        if (i > 0) {
            out += "===========================================\n";
        }

        // Header line
        out += "===========================================\n";
        out += "  锻造方案";
        if (!sol.is_success) {
            out += " [不可行]";
        }
        out += " - " + ench_summary_str(sol.original_ench) + "\n";
        out += "===========================================\n";

        // Mode and platform
        out += "模式: " + mode_display_name(mode_name) + "\n";
        out += "平台: " + platform_to_display(sol.platform) + "\n";

        // Rank
        out += "方案: " + std::to_string(i + 1) + "/" + std::to_string(solutions.size());
        if (i > 0 && sol.total_exp_level_cost == reference_cost) {
            out += " (同消耗)";
        }
        out += "\n\n";

        // Total cost
        out += "总消耗: " + std::to_string(sol.total_exp_level_cost) + " 等级 (" +
               std::to_string(sol.total_exp_cost) + " 经验值)\n";

        // Peak step
        if (sol.is_feasible()) {
            out += "峰值单步: Step " + std::to_string(sol.max_cost_step_index + 1) + " - " +
                   std::to_string(sol.get_peek_level_cost()) + " 等级 (" +
                   std::to_string(sol.get_peek_exp_cost()) + " 经验值)\n";
        }

        out += "\n输入:\n";
        out += "  目标物品: " + describe_item_verbose(sol.target_item) + "\n";
        out += "  可用物品:\n";
        for (const auto &item : sol.available_items) {
            out += "    - " + describe_item_verbose(item) + "\n";
        }

        out += "-------------------------------------------\n";
        for (size_t j = 0; j < sol.steps.size(); ++j) {
            const auto &step = sol.steps[j];

            // Compute result description (best-effort)
            std::string result_desc;
            try {
                EnchSet combined = step.item_a.enchantments.combine_s(step.item_b.enchantments);
                ItemStack result_item(step.item_a.equipment, combined,
                                      step.item_a.prior_penalty, step.item_a.durability);
                result_desc = describe_item_verbose(result_item);
            } catch (const std::exception &) {
                result_desc = "...";
            }

            out += "  Step " + std::to_string(j + 1) + ": " +
                   describe_item_verbose(step.item_a) + " + " +
                   describe_item_verbose(step.item_b) + " -> " +
                   result_desc + "\n";
            out += "          - 消耗: " + std::to_string(step.exp_level_cost) +
                   " 等级 (" + std::to_string(step.exp_cost) + " 经验值)\n";
        }
        out += "-------------------------------------------\n";
    }

    return out;
}

// ===========================================================================
// Format compact
// ===========================================================================
std::string OutputFormatter::format_compact(
    const std::vector<EnchSolution> &solutions,
    const std::string &mode_name
) {
    std::string out;
    out += "#MODE=" + mode_name + "\n";
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
            // Compute result description (best-effort)
            std::string result_desc;
            try {
                EnchSet combined = step.item_a.enchantments.combine_s(step.item_b.enchantments);
                ItemStack result_item(step.item_a.equipment, combined,
                                      step.item_a.prior_penalty, step.item_a.durability);
                result_desc = describe_item_compact(result_item);
            } catch (const std::exception &) {
                result_desc = "...";
            }

            out += std::to_string(i + 1) + "|" +          // solution rank
                   describe_item_compact(step.item_a) + "|" +
                   describe_item_compact(step.item_b) + "|" +
                   result_desc + "|" +
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
    const std::vector<EnchSolution> &solutions,
    const std::string &mode_name
) {
    Json::Object root;
    root["schema_version"] = Json(Json::String("1.0"));
    root["mode"]           = Json(Json::String(mode_name));

    Json::Array sol_arr;
    for (size_t si = 0; si < solutions.size(); ++si) {
        const auto &sol = solutions[si];
        Json::Object s;
        s["rank"] = Json(Json::Number(static_cast<int32_t>(si + 1)));

        // Platform
        switch (sol.platform) {
        case platform::MCE::Java:    s["platform"] = Json(Json::String("Java"));    break;
        case platform::MCE::Bedrock: s["platform"] = Json(Json::String("Bedrock")); break;
        case platform::MCE::All:     s["platform"] = Json(Json::String("All"));     break;
        default:                     s["platform"] = Json(Json::String("None"));    break;
        }

        // Original enchantments
        Json::Array orig_arr;
        for (const auto &ench : sol.original_ench) {
            Json::Object eo;
            eo["id"]    = Json(Json::String(ench_name_id(ench.id)));
            eo["level"] = Json(Json::Number(static_cast<int32_t>(ench.level)));
            orig_arr.push_back(Json(eo));
        }
        s["original_ench"] = Json(orig_arr);

        // Target item
        s["target_item"] = itemstack_to_json(sol.target_item);

        // Available items
        Json::Array avail_arr;
        for (const auto &item : sol.available_items) {
            avail_arr.push_back(itemstack_to_json(item));
        }
        s["available_items"] = Json(avail_arr);

        // Steps
        Json::Array steps_arr;
        for (const auto &step : sol.steps) {
            steps_arr.push_back(step_to_json(step));
        }
        s["steps"] = Json(steps_arr);

        // Summary
        s["total_exp_level_cost"] = Json(Json::Number(sol.total_exp_level_cost));
        s["total_exp_cost"]       = Json(Json::Number(sol.total_exp_cost));
        s["peek_level_cost"]      = Json(Json::Number(sol.get_peek_level_cost()));
        s["peek_exp_cost"]        = Json(Json::Number(sol.get_peek_exp_cost()));
        s["max_cost_step_index"]  = Json(Json::Number(static_cast<int64_t>(sol.max_cost_step_index)));
        s["is_success"]           = Json(Json::Bool(sol.is_success));

        // Metadata
        Json::Object meta;
        meta["algorithm_name"]   = Json(Json::String(sol.metadata.algorithm_name));
        meta["version"]          = Json(Json::String(sol.metadata.version));
        meta["created_at"]       = Json(Json::Number(static_cast<int64_t>(sol.metadata.created_at)));
        meta["computation_time"] = Json(Json::Number(static_cast<int64_t>(sol.metadata.computation_time)));
        s["metadata"]            = Json(meta);

        sol_arr.push_back(Json(s));
    }
    root["solutions"] = Json(sol_arr);

    return Json(root).to_string(Json::Pretty);
}

// ===========================================================================
// Parse JSON
// ===========================================================================
std::vector<EnchSolution> OutputFormatter::parse_json(const std::string &input) {
    // Clear equipment cache so parsed EquipmentType objects are fresh
    _json_eq_cache.clear();

    Json root  = Json::parse(input);
    Json::Value root_val = root.get_value();
    const Json::Object &root_obj = std::get<Json::Object>(root_val);

    // Solutions array
    auto sol_it = root_obj.find("solutions");
    if (sol_it == root_obj.end()) {
        return {};
    }
    Json::Value sol_arr_val = sol_it->second.get_value();
    const Json::Array &sol_arr = std::get<Json::Array>(sol_arr_val);

    std::vector<EnchSolution> results;
    results.reserve(sol_arr.size());

    for (const auto &sol_json : sol_arr) {
        Json::Value obj_val = sol_json.get_value();
        const Json::Object &obj = std::get<Json::Object>(obj_val);

        // Platform
        std::string plat_str = json_str(obj.at("platform"));
        platform::MCE plat = platform::MCE::None;
        if (plat_str == "Java")       plat = platform::MCE::Java;
        else if (plat_str == "Bedrock") plat = platform::MCE::Bedrock;
        else if (plat_str == "All")   plat = platform::MCE::All;

        // Original enchantments
        Json::Value orig_ench_val = obj.at("original_ench").get_value();
        EnchSet orig_ench = enchset_from_json_array(
            std::get<Json::Array>(orig_ench_val)
        );

        // Target item
        ItemStack target_item = itemstack_from_json(obj.at("target_item"), _json_eq_cache);

        // Available items
        ItemCollection avail_items;
        auto avail_it = obj.find("available_items");
        if (avail_it != obj.end()) {
            Json::Value avail_arr_val = avail_it->second.get_value();
            const Json::Array &avail_arr = std::get<Json::Array>(avail_arr_val);
            for (const auto &avail_j : avail_arr) {
                avail_items.push_back(itemstack_from_json(avail_j, _json_eq_cache));
            }
        }

        // Steps
        EnchStepList steps;
        auto steps_it = obj.find("steps");
        if (steps_it != obj.end()) {
            Json::Value steps_arr_val = steps_it->second.get_value();
            const Json::Array &steps_arr = std::get<Json::Array>(steps_arr_val);
            for (const auto &step_j : steps_arr) {
                steps.push_back(step_from_json(step_j, _json_eq_cache));
            }
        }

        // Metadata
        EnchSolution::MetaData meta;
        auto meta_it = obj.find("metadata");
        if (meta_it != obj.end()) {
            Json::Value meta_obj_val = meta_it->second.get_value();
            const Json::Object &meta_obj = std::get<Json::Object>(meta_obj_val);
            auto algo_it = meta_obj.find("algorithm_name");
            if (algo_it != meta_obj.end()) meta.algorithm_name = json_str(algo_it->second);
            auto ver_it = meta_obj.find("version");
            if (ver_it != meta_obj.end()) meta.version = json_str(ver_it->second);
            auto ca_it = meta_obj.find("created_at");
            if (ca_it != meta_obj.end()) meta.created_at = static_cast<size_t>(json_int64(ca_it->second));
            auto ct_it = meta_obj.find("computation_time");
            if (ct_it != meta_obj.end()) meta.computation_time = static_cast<size_t>(json_int64(ct_it->second));
        }

        // is_success
        bool is_success = true;
        auto succ_it = obj.find("is_success");
        if (succ_it != obj.end()) {
            Json::Value succ_val = succ_it->second.get_value();
            is_success = std::get<Json::Bool>(succ_val);
        }

        // Build solution via make() so costs are recomputed consistently
        results.push_back(EnchSolution::make(plat, orig_ench, target_item, avail_items, steps, is_success, meta));
    }

    return results;
}

// ===========================================================================
// JSON helpers
// ===========================================================================

// ---------------------------------------------------------------------------
// itemstack_to_json
// ---------------------------------------------------------------------------
Json OutputFormatter::itemstack_to_json(const ItemStack &item) {
    Json::Object obj;

    // Equipment
    if (item.equipment != nullptr) {
        Json::Object eq;
        eq["id"]             = Json(Json::String(item.equipment->id));
        eq["category"]       = Json(Json::String(item.equipment->category));
        eq["name"]           = Json(Json::String(item.equipment->name));
        eq["max_durability"] = Json(Json::Number(item.equipment->max_durability));
        obj["equipment"]     = Json(eq);
        obj["is_book"]       = Json(Json::Bool(false));
    } else {
        obj["equipment"] = Json::null();
        obj["is_book"]   = Json(Json::Bool(true));
    }

    // Enchantments
    Json::Array ench_arr;
    for (const auto &ench : item.enchantments) {
        Json::Object eo;
        eo["id"]    = Json(Json::String(ench_name_id(ench.id)));
        eo["level"] = Json(Json::Number(static_cast<int32_t>(ench.level)));
        ench_arr.push_back(Json(eo));
    }
    obj["enchantments"] = Json(ench_arr);

    obj["prior_penalty"] = Json(Json::Number(item.prior_penalty));
    obj["durability"]    = Json(Json::Number(item.durability));

    return Json(obj);
}

// ---------------------------------------------------------------------------
// itemstack_from_json
// ---------------------------------------------------------------------------
ItemStack OutputFormatter::itemstack_from_json(
    const Json &j,
    std::vector<EquipmentType> &equipment_cache
) {
    Json::Value j_val = j.get_value();
    const Json::Object &obj = std::get<Json::Object>(j_val);

    // Equipment (may be null for books)
    const EquipmentType *eq_ptr = nullptr;
    auto eq_it = obj.find("equipment");
    if (eq_it != obj.end()) {
        if (std::holds_alternative<Json::Null>(eq_it->second.get_value())) {
            eq_ptr = nullptr;
        } else {
            Json::Value eq_val = eq_it->second.get_value();
            const Json::Object &eq_obj = std::get<Json::Object>(eq_val);
            std::string id     = json_str(eq_obj.at("id"));
            std::string cat    = json_str(eq_obj.at("category"));
            std::string name   = json_str(eq_obj.at("name"));
            int32_t max_dur    = 0;
            auto md_it = eq_obj.find("max_durability");
            if (md_it != eq_obj.end()) {
                max_dur = json_int(md_it->second);
            }

            equipment_cache.emplace_back(EquipmentType{
                id, name, EquipmentCategory(cat.c_str()), max_dur
            });
            eq_ptr = &equipment_cache.back();
        }
    }

    // Enchantments
    EnchSet ench_set;
    auto ench_it = obj.find("enchantments");
    if (ench_it != obj.end()) {
        Json::Value ench_arr_val = ench_it->second.get_value();
        ench_set = enchset_from_json_array(
            std::get<Json::Array>(ench_arr_val)
        );
    }

    // Prior penalty
    int32_t prior_penalty = 0;
    auto pp_it = obj.find("prior_penalty");
    if (pp_it != obj.end()) {
        prior_penalty = json_int(pp_it->second);
    }

    // Durability
    int32_t durability = -1;
    auto dur_it = obj.find("durability");
    if (dur_it != obj.end()) {
        durability = json_int(dur_it->second);
    }

    return ItemStack(eq_ptr, ench_set, prior_penalty, durability);
}

// ---------------------------------------------------------------------------
// step_to_json
// ---------------------------------------------------------------------------
Json OutputFormatter::step_to_json(const EnchSolution::EnchStep &step) {
    Json::Object obj;
    obj["item_a"]         = itemstack_to_json(step.item_a);
    obj["item_b"]         = itemstack_to_json(step.item_b);
    obj["exp_level_cost"] = Json(Json::Number(step.exp_level_cost));
    obj["exp_cost"]       = Json(Json::Number(step.exp_cost));
    return Json(obj);
}

// ---------------------------------------------------------------------------
// step_from_json
// ---------------------------------------------------------------------------
EnchSolution::EnchStep OutputFormatter::step_from_json(
    const Json &j,
    std::vector<EquipmentType> &equipment_cache
) {
    Json::Value j_val = j.get_value();
    const Json::Object &obj = std::get<Json::Object>(j_val);
    EnchSolution::EnchStep step;
    step.item_a         = itemstack_from_json(obj.at("item_a"), equipment_cache);
    step.item_b         = itemstack_from_json(obj.at("item_b"), equipment_cache);
    step.exp_level_cost = json_int(obj.at("exp_level_cost"));
    step.exp_cost       = json_int(obj.at("exp_cost"));
    return step;
}
