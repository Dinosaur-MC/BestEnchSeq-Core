#include "McOfficialParser.h"
#include "ParserShared.h"
#include "builtin/DataLoader.h"
#include "domain/business/components/TagResolver.h"
#include "common/io/json.h"
#include "common/log/log.hpp"
#include "common/io/FileUtils.hpp"

#include <cctype>
#include <filesystem>
#include <unordered_map>
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
    auto fname_start = ench_pos + 13;  // length of "/enchantment/"
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

// ── Collect strings from a value that may be a single string OR an array ──
// Real MC 1.21+ datapack format allows fields like supported_items /
// exclusive_set to be a SINGLE STRING (e.g. "#minecraft:swords") OR an array
// of strings / "#"-prefixed tag refs. Old array-only parsing threw
// JsonException on the single-string form.

std::vector<std::string> collect_strings(const Json& v) {
    std::vector<std::string> out;
    auto val = v.get_value();
    if (auto* s = std::get_if<Json::String>(&val)) {
        out.push_back(*s);
    } else if (auto* arr = std::get_if<Json::Array>(&val)) {
        for (const auto& e : *arr) {
            auto e_val = e.get_value();
            if (auto* es = std::get_if<Json::String>(&e_val))
                out.push_back(*es);
        }
    }
    return out;
}

// ── Vanilla tag fallback ─────────────────────────────────────────────
// Seed the resolver with the builtin vanilla tags (full-path keys, e.g.
// `minecraft:enchantment/treasure`) so a datapack's canonical MC tag refs
// (`#minecraft:enchantment/treasure`) resolve even when the datapack does not
// define them.  The datapack's own tag files are loaded AFTER this seed, so a
// datapack override (same key, `replace: true`) still wins (TagResolver
// merge/replace semantics).
void seed_vanilla_tags(TagResolver& resolver) {
    for (const auto& [key, values] : besq::data::load_builtin_tag_entries())
        resolver.add_tag(key,
                         std::unordered_set<std::string>(values.begin(), values.end()));
}

/// Derive is_treasure from the `#minecraft:enchantment/treasure` tag.
/// MC datapack enchantment definitions carry no `is_treasure` field; treasure
/// membership is purely tag-driven.  Both the canonical vanilla key
/// (`minecraft:enchantment/treasure`) and the datapack category-dropped key
/// (`minecraft:treasure`, see parse_files key derivation) are checked so a
/// datapack's own treasure-tag override (additions / replacements) is honored.
bool is_treasure_member(const std::string& full_id, TagResolver& tag_resolver) {
    auto treasure = tag_resolver.resolve("#minecraft:enchantment/treasure");
    auto treasure_dp = tag_resolver.resolve("#minecraft:treasure");
    treasure.insert(treasure_dp.begin(), treasure_dp.end());
    return treasure.count(full_id) != 0;
}

} // anonymous namespace

// ============================================================================

business::loader::EnchantmentData McOfficialParser::parse_single_enchantment(
    const std::string& ns,
    const std::string& filename,
    const std::string& content,
    TagResolver& tag_resolver)
{
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

    // exclusive_set — real MC 1.21+ allows a single string OR array of
    // concrete IDs / "#tag" refs
    std::vector<std::string> excl_items;
    {
        auto it = obj.find("exclusive_set");
        if (it != obj.end())
            excl_items = collect_strings(it->second);
    }
    auto exclusive_set = business::parser_detail::resolve_references(excl_items, tag_resolver);

    // supported_items — real MC 1.21+ allows a single string OR array of
    // concrete IDs / "#tag" refs
    std::vector<std::string> supp_items;
    {
        auto it = obj.find("supported_items");
        if (it != obj.end())
            supp_items = collect_strings(it->second);
    }
    // supported_items 透传（真实 MC 格式：单字符串或数组、#tag 或具体 ID）。
    // limited_level 计算由注册表级 LimitedLevelCalculator 统一完成（B-T18），
    // 解析器只搬运 min_cost 原始字段。

    // min_cost — carried as raw fields; the registry-level calculator derives
    // limited_level from them uniformly across data sources.
    int32_t min_cost_base      = 0;
    int32_t min_cost_per_level = 0;
    auto min_cost_it = obj.find("min_cost");
    if (min_cost_it != obj.end()) {
        auto mc = min_cost_it->second.get_value();
        if (auto* mc_obj = std::get_if<Json::Object>(&mc)) {
            {
                auto it = mc_obj->find("base");
                if (it != mc_obj->end()) min_cost_base = it->second.as<int32_t>();
            }
            {
                auto it = mc_obj->find("per_level_above_first");
                if (it != mc_obj->end()) min_cost_per_level = it->second.as<int32_t>();
            }
        }
    }

    // Rare: a datapack enchant JSON may carry a legacy pre-computed
    // `limited_level` field — keep it symmetric with the native parser (hint
    // flag).  Otherwise the DTO defaults to max_level; the registry-level
    // LimitedLevelCalculator (B-T18) back-fills the real value at load.
    // is_treasure is carried as a data value (from the treasure tag), not
    // derived from limited_level.
    int32_t limited_level = max_level;
    bool limited_level_provided = false;
    auto ll_it = obj.find("limited_level");
    if (ll_it != obj.end()) {
        limited_level = ll_it->second.as<int32_t>();
        limited_level_provided = true;
    }

    business::loader::EnchantmentData ench;
    ench.id               = ns + ":" + filename;
    ench.display_name     = std::move(display_name);
    ench.multiplier       = multiplier;
    ench.max_level        = max_level;
    ench.limited_level    = limited_level;
    ench.limited_level_provided = limited_level_provided;
    ench.min_cost_base    = min_cost_base;
    ench.min_cost_per_level = min_cost_per_level;
    ench.is_treasure      = is_treasure_member(ench.id, tag_resolver);
    ench.exclusive_with.assign(exclusive_set.begin(), exclusive_set.end());
    ench.applicable_to    = std::move(supp_items);
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

std::vector<McOfficialParser::ItemTagDefinition>
McOfficialParser::extract_item_tag_definitions(
    const std::unordered_map<std::string, std::string>& tag_files)
{
    std::vector<ItemTagDefinition> result;
    for (const auto& [path, content] : tag_files) {
        std::string p = path;
        for (auto& c : p) { if (c == '\\') c = '/'; }

        // Only item tags drive item applicability (`tags_of`); enchantment
        // tags are out of scope for the profile tag universe (B-T14 I-1).
        if (p.find("/tags/item/") == std::string::npos)
            continue;

        auto tags_pos = p.find("/tags/");
        if (tags_pos == std::string::npos) continue;
        auto data_pos = p.find("data/");
        if (data_pos == std::string::npos) continue;
        auto ns_start = data_pos + 5;
        std::string ns = p.substr(ns_start, tags_pos - ns_start);
        auto category_end = p.find('/', tags_pos + 6);
        if (category_end == std::string::npos) continue;
        auto key_start = category_end + 1;
        std::string relative = p.substr(key_start);
        auto dot_pos = relative.find('.');
        if (dot_pos != std::string::npos)
            relative = relative.substr(0, dot_pos);

        try {
            Json json = Json::parse(content);
            auto root_var = json.get_value();
            if (!std::holds_alternative<Json::Object>(root_var)) continue;
            const auto& obj = std::get<Json::Object>(root_var);

            ItemTagDefinition def;
            def.key = ns + ":" + relative;

            auto replace_it = obj.find("replace");
            if (replace_it != obj.end()) {
                auto rv = replace_it->second.get_value();
                if (auto* b = std::get_if<Json::Bool>(&rv))
                    def.replace = *b;
            }

            auto values_it = obj.find("values");
            if (values_it != obj.end()) {
                auto vv = values_it->second.get_value();
                if (auto* arr = std::get_if<Json::Array>(&vv)) {
                    for (const auto& elem : *arr) {
                        auto ev = elem.get_value();
                        if (auto* s = std::get_if<Json::String>(&ev))
                            def.values.push_back(*s);
                    }
                }
            }
            result.push_back(std::move(def));
        } catch (...) {
            // Skip malformed tag files.
        }
    }
    return result;
}

// ============================================================================

TagRegistry McOfficialParser::build_item_tag_registry(
    const std::vector<ItemTagDefinition>& item_tags)
{
    TagRegistry reg;
    for (const auto& tag : item_tags) {
        try {
            reg.insert({NSID("#" + tag.key), tag.key});
        } catch (const std::exception&) {
            LOG_WARN("Skipping datapack item tag '%s': invalid tag id",
                     tag.key.c_str());
        }
    }
    return reg;
}

void McOfficialParser::load_item_tags_into(
    TagResolver& resolver, const std::vector<ItemTagDefinition>& item_tags)
{
    for (const auto& tag : item_tags) {
        // Skip tags whose ids fail NSID validation (same filter as
        // build_item_tag_registry) so a malformed tag never reaches the
        // resolver.
        try {
            (void)NSID("#" + tag.key);
        } catch (const std::exception&) {
            continue;
        }
        Json tag_json = Json::object();
        tag_json.set("replace", Json(tag.replace));
        Json values = Json::array();
        for (const auto& v : tag.values)
            values.push_back(Json(v));
        tag_json.set("values", std::move(values));
        resolver.load_tag_json(tag.key, tag_json);
    }
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

    // Seed the vanilla tag universe FIRST so canonical MC tag refs
    // (`#minecraft:enchantment/treasure`, `#minecraft:swords`, …) resolve even
    // when the datapack does not define them.  The datapack's own tag files are
    // loaded next and win on key collision (merge / `replace:true` semantics).
    seed_vanilla_tags(tag_resolver);

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

    // Extract the datapack's own item-tag definitions so they survive into the
    // profile's tag universe and TagResolver (B-T14 I-1).
    auto item_tags = extract_item_tag_definitions(tag_files);

    return {std::move(enchantments), std::move(equipment), std::move(item_tags)};
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
