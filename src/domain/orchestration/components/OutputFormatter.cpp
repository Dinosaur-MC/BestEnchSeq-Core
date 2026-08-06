#include "OutputFormatter.h"
#include "domain/business/business.h"
#include "domain/business/parsers/ParserShared.h"
#include "common/i18n/Language.h"
#include "common/i18n/NsidDisplay.h"
#include "domain/orchestration/components/OutputSchema.h"

bool OutputFormatter::_show_nsid = false;

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
// Enchantment name (i18n via Language system, fallback to NSID string)
// ---------------------------------------------------------------------------
std::string ench_display(const NSID& id) {
    auto name = ench_display_name(id);
    if (OutputFormatter::show_nsid()) {
        auto raw = id.str();
        if (name != raw)
            return name + " (" + raw + ")";
    }
    return name;
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
        result += ench_display(ench.id) + " " + to_roman(ench.level);
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
            ench_part += ench_display(ench.id) + " " + to_roman(ench.level);
        }
    }

    if (item.is_book()) {
        if (item.enchantments.empty()) {
            return item_display_name(item.id) + " " + tr("output.item.free");
        }
        return item_display_name(item.id) + "[" + ench_part + "]{ppn=" + std::to_string(item.prior_penalty) + "}";
    }

    // Equipment
    result = item_display_name(item.id);
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
            result += ench.id.str() + ":" + std::to_string(ench.level);
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
        result += ench.id.str() + ":" + std::to_string(ench.level);
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
    return ench_display(ench.id) + " " + to_roman(ench.level);
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

// Machine-readable raw platform name (compact / JSON must NOT be localized).
static std::string platform_to_raw(MCE p) {
    switch (p) {
    case MCE::Java:    return "Java";
    case MCE::Bedrock: return "Bedrock";
    case MCE::All:     return "All";
    default:           return "None";
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
        {
            std::string algo_line = tr_fmt("output.verbose.algorithm",
                sol.metadata.algorithm_name.empty() ? "?" : sol.metadata.algorithm_name);
            if (!sol.metadata.algorithm_version.empty())
                algo_line += tr_fmt("output.verbose.version", sol.metadata.algorithm_version);
            out += algo_line + "\n";
        }
        {
            auto ms = sol.metadata.computation_time.count();
            if (ms < 1000)
                out += tr_fmt("output.verbose.time_ms", ms) + "\n";
            else
                out += tr_fmt("output.verbose.time_s", ms / 1000, ms % 1000) + "\n";
        }
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

        // Goal already met: a successful 0-step solution (source already
        // satisfies the target) — render a clear message instead of an empty
        // steps list.
        if (sol.is_success && sol.steps.empty()) {
            out += "  " + tr("output.verbose.already_met") + "\n";
        }

        out += "\n" + tr("output.verbose.input_section") + "\n";
        // Source/available items first, then target item
        if (!sol.original_ench.empty() && mode == AlgorithmMode::direct) {
            out += tr_fmt("output.verbose.source_enchants", ench_summary_str(sol.original_ench, ench_reg)) + "\n";
        }
        if (!sol.available_items.empty()) {
            out += tr("output.verbose.available_items") + "\n";
            for (const auto &item : sol.available_items) {
                out += "    - " + describe_item_verbose(item, ench_reg) + "\n";
            }
        }
        out += tr_fmt("output.verbose.target_item", describe_item_verbose(sol.target_item, ench_reg)) + "\n";

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

    out += "#PLATFORM=" + platform_to_raw(solutions[0].platform) + "\n";
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

// ---------------------------------------------------------------------------
// build_json_root — shared root metadata object (CLI `--format json` + C ABI)
//
// Root schema (v1.1): {schema_version, mode, success, algorithm,
// computation_time_ms}.  Both OutputFormatter::format_json and the C ABI
// besq_solve build this object so the two outputs share one schema (the same
// ds field declarations in OutputSchema.h, assembled via
// ds::json::Schema<RootMetaSchema>::serialize).
// ---------------------------------------------------------------------------
Json OutputFormatter::build_json_root(AlgorithmMode mode, bool success,
                                      const std::string &algorithm,
                                      int64_t computation_time_ms) {
    RootMetaView root;
    root.schema_version      = kOutputSchemaVersion;
    root.mode                = mode_to_raw(mode);
    root.success             = success;
    root.algorithm           = algorithm;
    root.computation_time_ms = computation_time_ms;
    return ds::json::Schema<RootMetaSchema>::serialize(root);
}

std::string OutputFormatter::format_json(
    const std::vector<Solution> &solutions,
    const Profile &profile,
    AlgorithmMode mode,
    bool success,
    const std::string &algorithm,
    int64_t computation_time_ms
) {
    const auto &cat_reg = profile.tags();
    const auto &eq_reg  = profile.eq();

    // Root metadata defaults: when the caller has no SolveResult (e.g. a bare
    // solution export), derive the algorithm name / time from the first
    // solution so the root fields stay populated.
    std::string algo    = algorithm;
    int64_t time_ms     = computation_time_ms;
    if (algo.empty() && !solutions.empty())
        algo = solutions[0].metadata.algorithm_name;
    if (time_ms <= 0 && !solutions.empty())
        time_ms = solutions[0].metadata.computation_time.count();

    // The whole root — metadata + solutions — is assembled by the ds output
    // schema (OutputSchema.h).  `ench_reg` is not needed on the encode side:
    // items resolve name/category/durability from the equipment + tag
    // registries only (mirrors the parse side, which validates against the
    // enchantment registry).
    RootView root;
    root.schema_version      = kOutputSchemaVersion;
    root.mode                = mode_to_raw(mode);
    root.success             = success;
    root.algorithm           = algo;
    root.computation_time_ms = time_ms;
    root.solutions.reserve(solutions.size());
    for (size_t si = 0; si < solutions.size(); ++si) {
        root.solutions.push_back(
            make_solution_view(solutions[si], static_cast<int32_t>(si + 1),
                               cat_reg, eq_reg));
    }

    return ds::json::Schema<RootSchema>::serialize(root).to_string(Json::Pretty);
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
// make_item_view
// ---------------------------------------------------------------------------
ItemView OutputFormatter::make_item_view(
    const Item &item,
    const TagRegistry &cat_reg,
    const EquipmentRegistry &eq_reg
) {
    ItemView out;
    out.is_book = item.is_book();

    // Equipment
    if (!item.is_book()) {
        EquipmentView eq;
        eq.id             = item.id.str();
        eq.category       = "unknown";
        eq.name           = item_display_name(item.id);
        eq.max_durability = 0;

        // Fill in the real equipment data when the item id is registered.
        // `category` is the equipment's display short name (e.g. "sword"),
        // resolved from the equipment's category NSID via the tag registry,
        // falling back to the NSID's short form (mirrors RegistryLoader).
        // Unknown ids keep the sensible defaults above (books are is_book:true
        // with equipment:null already).
        if (auto eq_it = eq_reg.find(item.id); eq_it != eq_reg.end()) {
            eq.max_durability = eq_it->max_durability;
            std::string category_name;
            if (auto tag_it = cat_reg.find(eq_it->category);
                tag_it != cat_reg.end()) {
                category_name = tag_it->name;
            } else {
                category_name =
                    business::parser_detail::category_short_name(eq_it->category);
                if (category_name.empty())
                    category_name = "unknown";
            }
            eq.category = std::move(category_name);
        }
        out.equipment = std::move(eq);
    }

    // Enchantments
    out.enchantments.reserve(item.enchantments.size());
    for (const auto &ench : item.enchantments) {
        EnchView eo;
        eo.id    = ench.id.str();
        eo.level = ench.level;
        out.enchantments.push_back(std::move(eo));
    }

    out.prior_penalty = item.prior_penalty;
    out.durability    = item.durability;
    return out;
}

// ---------------------------------------------------------------------------
// Item_from_json
// ---------------------------------------------------------------------------
Item OutputFormatter::item_from_json(
    const Json &j,
    std::vector<Equipment> &equipment_cache,
    const EnchantmentRegistry &ench_reg,
    const TagRegistry &cat_reg
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
// make_step_view / make_solution_view
// ---------------------------------------------------------------------------
StepView OutputFormatter::make_step_view(
    const Solution::EnchStep &step,
    const TagRegistry &cat_reg,
    const EquipmentRegistry &eq_reg
) {
    StepView out;
    out.item_a         = make_item_view(step.item_a, cat_reg, eq_reg);
    out.item_b         = make_item_view(step.item_b, cat_reg, eq_reg);
    out.result         = make_item_view(step.result, cat_reg, eq_reg);
    out.exp_level_cost = step.exp_level_cost;
    out.exp_cost       = step.exp_cost;
    return out;
}

SolutionView OutputFormatter::make_solution_view(
    const Solution &sol,
    int32_t rank,
    const TagRegistry &cat_reg,
    const EquipmentRegistry &eq_reg
) {
    SolutionView out;
    out.rank     = rank;
    out.platform = platform_to_raw(sol.platform);

    // Original enchantments
    out.original_ench.reserve(sol.original_ench.size());
    for (const auto &ench : sol.original_ench) {
        EnchView eo;
        eo.id    = ench.id.str();
        eo.level = ench.level;
        out.original_ench.push_back(std::move(eo));
    }

    // Target item
    out.target_item = make_item_view(sol.target_item, cat_reg, eq_reg);

    // Available items
    out.available_items.reserve(sol.available_items.size());
    for (const auto &item : sol.available_items) {
        out.available_items.push_back(make_item_view(item, cat_reg, eq_reg));
    }

    // Steps
    out.steps.reserve(sol.steps.size());
    for (const auto &step : sol.steps) {
        out.steps.push_back(make_step_view(step, cat_reg, eq_reg));
    }

    // Summary
    out.total_exp_level_cost = sol.total_exp_level_cost;
    out.total_exp_cost       = sol.total_exp_cost;
    out.peak_level_cost      = sol.get_peak_level_cost();
    out.peak_exp_cost        = sol.get_peak_exp_cost();
    out.max_cost_step_index  = static_cast<int64_t>(sol.max_cost_step_index);
    out.is_success           = sol.is_success;

    // Metadata
    out.metadata.algorithm_name    = sol.metadata.algorithm_name;
    out.metadata.algorithm_version = sol.metadata.algorithm_version;
    out.metadata.created_at = static_cast<int64_t>(
        sol.metadata.created_at.time_since_epoch().count());
    out.metadata.computation_time = static_cast<int64_t>(
        sol.metadata.computation_time.count());
    return out;
}

// ---------------------------------------------------------------------------
// step_from_json
// ---------------------------------------------------------------------------
Solution::EnchStep OutputFormatter::step_from_json(
    const Json &j,
    std::vector<Equipment> &equipment_cache,
    const EnchantmentRegistry &ench_reg,
    const TagRegistry &cat_reg
) {
    Solution::EnchStep step;
    step.item_a         = item_from_json(j["item_a"], equipment_cache, ench_reg, cat_reg);
    step.item_b         = item_from_json(j["item_b"], equipment_cache, ench_reg, cat_reg);
    if (j.has("result"))
        step.result     = item_from_json(j["result"], equipment_cache, ench_reg, cat_reg);
    step.exp_level_cost = json_int(j["exp_level_cost"]);
    step.exp_cost       = json_int(j["exp_cost"]);
    return step;
}
