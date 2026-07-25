#include "McOfficialParser.h"
#include "ParserShared.h"
#include "domain/business/components/TagResolver.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "common/io/FileUtils.hpp"

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

// ── Parse a single tag's "values" array into a set of item IDs ────────

std::unordered_set<std::string> extract_item_ids_from_tag(const std::string& content) {
    std::unordered_set<std::string> result;
    try {
        Json json = Json::parse(content);
        auto root_var = json.get_value();
        if (!std::holds_alternative<Json::Object>(root_var)) return result;
        const auto& root_obj = std::get<Json::Object>(root_var);

        auto values_it = root_obj.find("values");
        if (values_it == root_obj.end()) return result;
        auto values_var = values_it->second.get_value();
        if (!std::holds_alternative<Json::Array>(values_var)) return result;
        const auto& values_arr = std::get<Json::Array>(values_var);

        for (const auto& elem : values_arr) {
            auto elem_var = elem.get_value();
            if (std::holds_alternative<Json::String>(elem_var)) {
                std::string val = std::get<Json::String>(elem_var);
                if (!val.empty() && val[0] != '#') {
                    result.insert(val);
                }
            }
        }
    } catch (...) {}
    return result;
}

// ── Collect all unique item IDs from a set of item tag files ──────────

std::unordered_set<std::string> collect_item_ids_from_tags(
    const std::unordered_map<std::string, std::string>& tag_files)
{
    std::unordered_set<std::string> seen_ids;
    std::unordered_set<std::string> item_ids;

    for (const auto& [path, content] : tag_files) {
        // Only process files under tags/item/
        if (path.find("/tags/item/") == std::string::npos &&
            path.find("\\tags\\item\\") == std::string::npos)
            continue;

        auto ids = extract_item_ids_from_tag(content);
        for (const auto& id : ids) {
            if (seen_ids.insert(id).second)
                item_ids.insert(id);
        }
    }
    return item_ids;
}

// ── Extract namespace and filename from a data-pack path ──────────────

bool parse_enchantment_path(const std::string& path,
                            std::string& out_ns,
                            std::string& out_filename)
{
    // Expected: "data/<ns>/enchantment/<id>.json"
    // Also accept backslash on Windows
    std::string p = path;
    for (auto& c : p) { if (c == '\\') c = '/'; }

    // Find "/enchantment/" segment
    auto ench_pos = p.find("/enchantment/");
    if (ench_pos == std::string::npos) return false;

    // Extract namespace: after "data/" up to "/enchantment/"
    auto data_pos = p.find("data/");
    if (data_pos == std::string::npos) return false;
    auto ns_start = data_pos + 5;  // length of "data/"
    out_ns = p.substr(ns_start, ench_pos - ns_start);

    // Extract filename (without extension)
    auto fname_start = ench_pos + 12;  // length of "/enchantment/"
    auto dot_pos = p.find('.', fname_start);
    if (dot_pos == std::string::npos) {
        out_filename = p.substr(fname_start);
    } else {
        out_filename = p.substr(fname_start, dot_pos - fname_start);
    }
    return true;
}

// ── Check if a path is a tag file ─────────────────────────────────────

bool is_tag_file(const std::string& path) {
    return path.find("/tags/") != std::string::npos ||
           path.find("\\tags\\") != std::string::npos;
}

} // anonymous namespace

// ============================================================================

business::loader::EnchantmentData McOfficialParser::parse_single_enchantment(
    const std::string& ns,
    const std::string& filename,
    const std::string& content,
    TagResolver& tag_resolver)
{
    auto item_props = load_item_properties();
    Json root;
    try {
        root = Json::parse(content);
    } catch (...) {
        LOG_WARN("Could not parse enchantment %s:%s", ns.c_str(), filename.c_str());
        business::loader::EnchantmentData empty;
        empty.id = ns + ":" + filename;
        return empty;
    }

    auto root_var = root.get_value();
    if (!std::holds_alternative<Json::Object>(root_var)) {
        business::loader::EnchantmentData empty;
        empty.id = ns + ":" + filename;
        return empty;
    }
    const auto& obj = std::get<Json::Object>(root_var);

    int32_t multiplier = 0, max_level = 0;
    {
        auto it = obj.find("anvil_cost");
        if (it != obj.end()) multiplier = it->second.as<int32_t>();
    }
    {
        auto it = obj.find("max_level");
        if (it != obj.end()) max_level = it->second.as<int32_t>();
    }

    if (max_level <= 0 || multiplier <= 0) {
        LOG_WARN("Skipping %s:%s (max_level=%d, anvil_cost=%d)",
                 ns.c_str(), filename.c_str(), max_level, multiplier);
        business::loader::EnchantmentData empty;
        empty.id = ns + ":" + filename;
        return empty;
    }

    std::string display_name = business::parser_detail::derive_display_name(filename);

    // exclusive_set
    std::vector<std::string> excl_items;
    {
        auto it = obj.find("exclusive_set");
        if (it != obj.end()) {
            Json::Array arr = it->second.as<Json::Array>();
            for (const auto& elem : arr)
                excl_items.push_back(elem.as<std::string>());
        }
    }
    auto exclusive_set = business::parser_detail::resolve_references(excl_items, tag_resolver);

    // supported_items
    std::vector<std::string> supp_items;
    {
        auto it = obj.find("supported_items");
        if (it != obj.end()) {
            Json::Array arr = it->second.as<Json::Array>();
            for (const auto& elem : arr)
                supp_items.push_back(elem.as<std::string>());
        }
    }
    auto applicable_items = business::parser_detail::resolve_references(supp_items, tag_resolver);

    // Compute limited_level from cost formula
    int32_t limited_level = max_level;
    auto min_cost_it = obj.find("min_cost");
    if (min_cost_it != obj.end()) {
        auto mc = min_cost_it->second.get_value();
        if (auto* mc_obj = std::get_if<Json::Object>(&mc)) {
            int32_t min_base      = 0;
            int32_t min_per_level = 0;
            {
                auto it = mc_obj->find("base");
                if (it != mc_obj->end()) min_base = it->second.as<int32_t>();
            }
            {
                auto it = mc_obj->find("per_level_above_first");
                if (it != mc_obj->end()) min_per_level = it->second.as<int32_t>();
            }
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
    return ench;
}

// ============================================================================

std::vector<business::loader::EquipmentData>
McOfficialParser::derive_equipment_from_tag_files(
    const std::unordered_map<std::string, std::string>& tag_files)
{
    auto item_props = load_item_properties();
    auto item_ids = collect_item_ids_from_tags(tag_files);

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

// ============================================================================

McOfficialParser::Result McOfficialParser::parse_files(
    const std::unordered_map<std::string, std::string>& files)
{
    TagResolver tag_resolver;
    std::unordered_map<std::string, std::string> enchantment_files;
    std::unordered_map<std::string, std::string> tag_files;

    // Separate files into tags, enchantments, and equipment tags
    for (const auto& [path, content] : files) {
        if (is_tag_file(path)) {
            tag_files[path] = content;
        }
    }

    // Load all tag files into the TagResolver
    for (const auto& [path, content] : tag_files) {
        // Derive tag key from path: "data/<ns>/tags/<category>/<rest>.json" → "<ns>:<rest>"
        std::string p = path;
        for (auto& c : p) { if (c == '\\') c = '/'; }

        auto tags_pos = p.find("/tags/");
        if (tags_pos == std::string::npos) continue;

        // Extract namespace
        auto data_pos = p.find("data/");
        if (data_pos == std::string::npos) continue;
        auto ns_start = data_pos + 5;
        std::string ns = p.substr(ns_start, tags_pos - ns_start);

        // Skip past "/tags/<category>/" to get the tag key path
        auto category_end = p.find('/', tags_pos + 6);
        if (category_end == std::string::npos) continue;
        auto key_start = category_end + 1;

        // Relative path without extension
        std::string relative = p.substr(key_start);
        auto dot_pos = relative.find('.');
        if (dot_pos != std::string::npos)
            relative = relative.substr(0, dot_pos);

        std::string tag_key = ns + ":" + relative;
        tag_resolver.load_tag_content(tag_key, content);
    }

    // Parse enchantment files
    std::vector<business::loader::EnchantmentData> enchantments;
    for (const auto& [path, content] : files) {
        std::string ns, filename;
        if (parse_enchantment_path(path, ns, filename)) {
            auto ench = parse_single_enchantment(ns, filename, content, tag_resolver);
            if (ench.max_level > 0 && ench.multiplier > 0)
                enchantments.push_back(std::move(ench));
        }
    }

    // Derive equipment from tag files
    auto equipment = derive_equipment_from_tag_files(tag_files);

    return {std::move(enchantments), std::move(equipment)};
}

// ============================================================================

McOfficialParser::Result McOfficialParser::parse(const std::filesystem::path& directory) {
    std::unordered_map<std::string, std::string> files;

    std::filesystem::path data_dir = directory / "data";
    if (!std::filesystem::is_directory(data_dir)) return {};

    // Recursively discover all JSON files under data/
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             data_dir, std::filesystem::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".json") continue;

        // Compute relative path from data/ directory
        std::string relative = std::filesystem::relative(entry.path(), data_dir).string();
        for (auto& c : relative) {
            if (c == '\\') c = '/';
        }
        std::string full_path = "data/" + relative;

        try {
            files[full_path] = file_utils::read_file(entry.path());
        } catch (...) {
            LOG_WARN("Could not read %s", entry.path().c_str());
            continue;
        }
    }

    return parse_files(files);
}
