#include "McOfficialParser.h"
#include "ParserShared.h"
#include "domain/business/components/TagResolver.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "common/utils/ParserUtils.hpp"

#include <cctype>
#include <filesystem>
#include <unordered_set>
#include <vector>

namespace {

// ── Durability lookup ─────────────────────────────────────────────────

int32_t get_durability(const std::string& item_id,
                       const std::unordered_map<std::string, ItemProperty>& props)
{
    std::string key = item_id;
    auto colon = key.find(':');
    if (colon != std::string::npos)
        key = key.substr(colon + 1);
    auto it = props.find(key);
    return (it != props.end()) ? it->second.durability : 0;
}

// ── Category derivation ───────────────────────────────────────────────

std::string derive_category(const std::string& item_id,
                            const std::unordered_map<std::string, ItemProperty>& props)
{
    std::string key = item_id;
    auto colon = key.find(':');
    if (colon != std::string::npos)
        key = key.substr(colon + 1);
    auto it = props.find(key);
    if (it != props.end() && !it->second.category.empty())
        return it->second.category;
    return business::parser_detail::get_category_suffix(item_id);
}

// ── Derive equipment from item tag files ──────────────────────────────

std::vector<business::loader::EquipmentData> derive_equipment_from_tags(
    const std::filesystem::path& data_dir,
    const std::unordered_map<std::string, ItemProperty>& item_props)
{
    std::unordered_set<std::string> item_ids;
    std::unordered_set<std::string> seen_ids;

    for (const auto& ns_entry : std::filesystem::directory_iterator(
             data_dir, std::filesystem::directory_options::skip_permission_denied)) {
        if (!ns_entry.is_directory()) continue;

        std::filesystem::path tags_item_dir = ns_entry.path() / "tags" / "item";
        if (!std::filesystem::is_directory(tags_item_dir)) continue;

        try {
            for (const auto& file_entry : std::filesystem::recursive_directory_iterator(
                     tags_item_dir, std::filesystem::directory_options::skip_permission_denied)) {
                if (!file_entry.is_regular_file()) continue;
                if (file_entry.path().extension() != ".json") continue;

                try {
                    std::string content = ParserUtils::read_file(file_entry.path());
                    Json json           = Json::parse(content);
                    auto root_var       = json.get_value();
                    if (!std::holds_alternative<Json::Object>(root_var)) continue;
                    const auto& root_obj = std::get<Json::Object>(root_var);

                    auto values_it = root_obj.find("values");
                    if (values_it == root_obj.end()) continue;
                    auto values_var = values_it->second.get_value();
                    if (!std::holds_alternative<Json::Array>(values_var)) continue;
                    const auto& values_arr = std::get<Json::Array>(values_var);

                    for (const auto& elem : values_arr) {
                        auto elem_var = elem.get_value();
                        if (std::holds_alternative<Json::String>(elem_var)) {
                            std::string val = std::get<Json::String>(elem_var);
                            if (!val.empty() && val[0] != '#') {
                                if (seen_ids.insert(val).second)
                                    item_ids.insert(val);
                            }
                        }
                    }
                } catch (...) { continue; }
            }
        } catch (const std::filesystem::filesystem_error&) { continue; }
    }

    std::vector<business::loader::EquipmentData> result;
    for (const auto& item_id : item_ids) {
        int32_t durability   = get_durability(item_id, item_props);
        std::string category = derive_category(item_id, item_props);

        if (durability <= 0 && category == item_id.substr(item_id.find(':') + 1)) {
            if (item_id.find(':') == std::string::npos) continue;
        }

        business::loader::EquipmentData eq;
        eq.id             = item_id;
        eq.display_name   = business::parser_detail::derive_display_name(item_id);
        eq.category       = category;
        eq.max_durability = durability;
        result.push_back(std::move(eq));
    }

    return result;
}

} // anonymous namespace

// ============================================================================

McOfficialParser::Result McOfficialParser::parse(const std::filesystem::path& directory) {
    TagResolver tag_resolver;
    tag_resolver.load_from(directory);

    auto item_props = load_item_properties();

    std::vector<business::loader::EnchantmentData> enchantments;

    std::filesystem::path data_dir = directory / "data";
    if (!std::filesystem::is_directory(data_dir)) return {};

    for (const auto& ns_entry : std::filesystem::directory_iterator(
             data_dir, std::filesystem::directory_options::skip_permission_denied)) {
        if (!ns_entry.is_directory()) continue;

        std::string ns = ns_entry.path().filename().string();
        std::filesystem::path ench_dir = ns_entry.path() / "enchantment";
        if (!std::filesystem::is_directory(ench_dir)) continue;

        for (const auto& ench_file : std::filesystem::directory_iterator(
                 ench_dir, std::filesystem::directory_options::skip_permission_denied)) {
            if (!ench_file.is_regular_file()) continue;

            std::string ext = ench_file.path().extension().string();
            for (auto& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".json") continue;

            std::string filename = ench_file.path().stem().string();

            std::string content;
            try {
                content = ParserUtils::read_file(ench_file.path());
            } catch (...) {
                LOG_WARN("Could not read %s", ench_file.path().c_str());
                continue;
            }

            Json root;
            try {
                root = Json::parse(content);
            } catch (...) {
                LOG_WARN("Could not parse %s", ench_file.path().c_str());
                continue;
            }

            auto root_var = root.get_value();
            if (!std::holds_alternative<Json::Object>(root_var)) continue;
            const auto& obj = std::get<Json::Object>(root_var);

            int32_t multiplier = ParserUtils::get_json_int(obj, "anvil_cost");
            int32_t max_level  = ParserUtils::get_json_int(obj, "max_level");

            if (max_level <= 0 || multiplier <= 0) {
                LOG_WARN("Skipping %s:%s (max_level=%d, anvil_cost=%d)",
                         ns.c_str(), filename.c_str(), max_level, multiplier);
                continue;
            }

            std::string display_name = business::parser_detail::derive_display_name(filename);

            auto excl_items    = ParserUtils::get_json_string_array(obj, "exclusive_set");
            auto exclusive_set = business::parser_detail::resolve_references(excl_items, tag_resolver);

            auto supp_items       = ParserUtils::get_json_string_array(obj, "supported_items");
            auto applicable_items = business::parser_detail::resolve_references(supp_items, tag_resolver);

            // Compute limited_level from cost formula
            int32_t limited_level = max_level;
            auto min_cost_it = obj.find("min_cost");
            if (min_cost_it != obj.end()) {
                auto mc = min_cost_it->second.get_value();
                if (auto* mc_obj = std::get_if<Json::Object>(&mc)) {
                    int32_t min_base      = ParserUtils::get_json_int(*mc_obj, "base");
                    int32_t min_per_level = ParserUtils::get_json_int(*mc_obj, "per_level_above_first");
                    if (min_base > 0 && min_per_level >= 0) {
                        limited_level = business::parser_detail::compute_limited_level(
                            max_level, min_base, min_per_level, applicable_items, item_props
                        );
                    }
                }
            }

            business::loader::EnchantmentData ench;
            ench.id               = ns + ":" + filename;
            ench.display_name     = std::move(display_name);
            ench.multiplier       = multiplier;
            ench.max_level        = max_level;
            ench.limited_level    = limited_level;
            ench.exclusive_with.assign(exclusive_set.begin(), exclusive_set.end());
            ench.applicable_to.assign(applicable_items.begin(), applicable_items.end());
            enchantments.push_back(std::move(ench));
        }
    }

    auto equipment = derive_equipment_from_tags(data_dir, item_props);

    return {std::move(enchantments), std::move(equipment)};
}
