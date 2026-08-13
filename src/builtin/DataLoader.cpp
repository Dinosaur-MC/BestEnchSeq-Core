#include "DataLoader.h"
#include "common/io/FileUtils.hpp"
#include "common/io/json.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/parsers/NativeJsonParser.h"
#include "EmbeddedData.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace besq::data {

namespace {

/// Read the builtin vanilla.json raw content once (filesystem override or
/// embedded), so the tag seed and the DTO parse come from the same single
/// read.
std::string read_builtin_content(const std::filesystem::path& data_dir) {
    auto vanilla_path = data_dir / "vanilla.json";
    if (std::filesystem::exists(vanilla_path))
        return file_utils::read_file(vanilla_path);
    return std::string{vanilla_json()};
}

/// Parse the builtin vanilla.json `tags` object into {key, values} pairs.
/// Tag keys follow the "<ns>:<tagpath>" convention — e.g. "minecraft:swords",
/// "minecraft:enchantable/sharp_weapon" — and values are the raw array
/// entries (concrete IDs or `#`-references), preserved verbatim so nested
/// tag expansion happens lazily at resolution time.
std::vector<std::pair<std::string, std::vector<std::string>>> parse_tag_entries(const std::string& content) {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    try {
        Json root = Json::parse(content);
        auto root_var = root.get_value();
        if (!std::holds_alternative<Json::Object>(root_var))
            return out;
        const auto& root_obj = std::get<Json::Object>(root_var);
        auto tags_it = root_obj.find("tags");
        if (tags_it == root_obj.end())
            return out;
        auto tags_var = tags_it->second.get_value();
        if (!std::holds_alternative<Json::Object>(tags_var))
            return out;
        for (const auto& [key, val] : std::get<Json::Object>(tags_var)) {
            auto val_var = val.get_value();
            if (!std::holds_alternative<Json::Array>(val_var))
                continue;
            std::vector<std::string> values;
            for (const auto& elem : std::get<Json::Array>(val_var)) {
                auto e = elem.get_value();
                if (auto* s = std::get_if<Json::String>(&e))
                    values.push_back(*s);
            }
            out.emplace_back(key, std::move(values));
        }
    } catch (...) {
        // best-effort — a malformed override yields no tags
    }
    return out;
}

/// Seed a TagRegistry from the dataset's REAL tag definitions (the `tags`
/// object of vanilla.json — real MC item + enchantment tags).  This is the
/// vanilla fallback tag universe: a `#tag` supported_items reference only
/// survives cross-validation when it is defined here.
/// TODO(T10): replaces the T6 stopgap that derived synthetic
/// `#minecraft:<category>` tags from the equipment categories array.
TagRegistry parse_base_tags(const std::string& content) {
    TagRegistry base_tags;
    for (const auto& [key, values] : parse_tag_entries(content)) {
        (void)values;
        base_tags.insert({NSID("#" + key), key});
    }
    return base_tags;
}

} // namespace

std::vector<std::pair<std::string, std::vector<std::string>>> load_builtin_tag_entries(const std::filesystem::path& data_dir) {
    // Process-lifetime cache: the builtin vanilla.json is static for a given
    // data_dir, so parse it once and reuse — avoids re-parsing ~92 KB on every
    // profile load and keeps the parser seed / DataLoader seed consistent.
    // Profile loading is single-threaded in this codebase.
    static std::unordered_map<std::string, std::vector<std::pair<std::string, std::vector<std::string>>>> cache;
    const std::string key = data_dir.string();
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;
    auto entries = parse_tag_entries(read_builtin_content(data_dir));
    cache.emplace(key, entries);
    return entries;
}

std::shared_ptr<TagResolver> make_builtin_tag_resolver(const std::filesystem::path& data_dir) {
    auto resolver = std::make_shared<TagResolver>();
    for (const auto& [key, values] : load_builtin_tag_entries(data_dir))
        resolver->add_tag(key, std::unordered_set<std::string>(values.begin(), values.end()));
    return resolver;
}

ProfileMetadata load_builtin_metadata(const std::filesystem::path& data_dir) {
    ProfileMetadata meta;
    try {
        Json root = Json::parse(read_builtin_content(data_dir));
        if (root.type() != JsonType::Object)
            return meta;
        auto read_str = [&root](std::string_view key) {
            return root.has(std::string(key)) ? root[std::string(key)].as<std::string>() : std::string{};
        };
        meta.name = read_str(ProfileMetadata::KEY_NAME);
        meta.display_name = read_str(ProfileMetadata::KEY_DISPLAY_NAME);
        meta.description = read_str(ProfileMetadata::KEY_DESCRIPTION);
        meta.author = read_str(ProfileMetadata::KEY_AUTHOR);
        meta.version = read_str(ProfileMetadata::KEY_VERSION);
        meta.mc_version = read_str(ProfileMetadata::KEY_MC_VERSION);
        meta.parent = read_str(ProfileMetadata::KEY_PARENT);
        if (root.has(std::string(ProfileMetadata::KEY_DEPENDENCIES))) {
            Json dep_val = root[std::string(ProfileMetadata::KEY_DEPENDENCIES)];
            if (dep_val.type() == JsonType::Array)
                for (const auto& e : dep_val.as_array())
                    meta.dependencies.push_back(e.as<std::string>());
        }
        meta.created_at = std::chrono::system_clock::now();
        meta.updated_at = meta.created_at;
    } catch (const std::exception&) {
        // best-effort — a malformed override yields default (empty) metadata
    }
    return meta;
}

void load_builtin_data(TagRegistry& tag_reg,
                       EnchantmentRegistry& ench_reg,
                       EquipmentRegistry& eq_reg,
                       const std::filesystem::path& data_dir) {
    auto vanilla_path = data_dir / "vanilla.json";
    RegistryLoader loader;

    // Read the raw content once so the declared categories can seed the
    // vanilla fallback tag universe.
    const std::string content = read_builtin_content(data_dir);
    const bool from_fs = std::filesystem::exists(vanilla_path);
    TagRegistry base_tags = parse_base_tags(content);

    if (from_fs) {
        // Filesystem path: allows user to replace builtin data (any supported
        // format; for non-JSON overrides the categories array is simply empty).
        auto parsed = FormatDetector::parse(vanilla_path);
        loader.resolve(parsed.enchantments, parsed.equipment, tag_reg, eq_reg, ench_reg, &base_tags);
    } else {
        // Embedded fallback: zero I/O, always available (native JSON).
        auto parsed = NativeJsonParser::parse_string(content);
        loader.resolve(parsed.first, parsed.second, tag_reg, eq_reg, ench_reg, &base_tags);
    }
}

} // namespace besq::data
