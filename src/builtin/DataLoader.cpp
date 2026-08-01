#include "DataLoader.h"
#include "EmbeddedData.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/parsers/NativeJsonParser.h"
#include "common/io/FileUtils.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace besq::data {

namespace {

/// Read the builtin vanilla.json raw content once (filesystem override or
/// embedded).  Shared by load_builtin_data and load_builtin_dtos so the
/// categories seed and the DTO parse come from the same single read.
std::string read_builtin_content(const std::filesystem::path& data_dir) {
    auto vanilla_path = data_dir / "vanilla.json";
    if (std::filesystem::exists(vanilla_path))
        return file_utils::read_file(vanilla_path);
    return std::string{vanilla_json()};
}

/// Seed a TagRegistry from the dataset's FULL declared `categories` array
/// (all entries, including categories with no concrete equipment like
/// "spear"/"head").  This is the vanilla fallback tag universe; the
/// enchantments' `#minecraft:<category>` supported_items references are
/// cross-validated against it in RegistryLoader::from_dto.
/// TODO(T10): this is a stopgap — once vanilla.json is regenerated with
/// real MC item-tag definitions, seed base_tags from those tags instead.
TagRegistry parse_base_tags(const std::string& content) {
    TagRegistry base_tags;
    for (const auto& cat : NativeJsonParser::parse_categories_string(content))
        base_tags.insert({NSID("#minecraft:" + cat), cat});
    return base_tags;
}

} // namespace

void load_builtin_dtos(
    std::vector<business::loader::EnchantmentData>& ench,
    std::vector<business::loader::EquipmentData>& eq)
{
    // Same single raw-content read as load_builtin_data (filesystem override
    // or embedded), so the validation universe matches the builtin registries.
    std::string content = read_builtin_content("data/builtin");
    auto parsed = NativeJsonParser::parse_string(content);
    ench = std::move(parsed.first);
    eq = std::move(parsed.second);
}

void load_builtin_data(
    TagRegistry& tag_reg,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    const std::filesystem::path& data_dir
) {
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
        loader.resolve(parsed.first, parsed.second, tag_reg, eq_reg, ench_reg, &base_tags);
    } else {
        // Embedded fallback: zero I/O, always available (native JSON).
        auto parsed = NativeJsonParser::parse_string(content);
        loader.resolve(parsed.first, parsed.second, tag_reg, eq_reg, ench_reg, &base_tags);
    }
}

} // namespace besq::data
