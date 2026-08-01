#include "DataLoader.h"
#include "EmbeddedData.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/parsers/NativeJsonParser.h"

#include <filesystem>
#include <string>
#include <unordered_set>

namespace besq::data {

void load_builtin_data(
    TagRegistry& tag_reg,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    const std::filesystem::path& data_dir
) {
    auto vanilla_path = data_dir / "vanilla.json";
    RegistryLoader loader;

    std::vector<business::loader::EnchantmentData> ench_data;
    std::vector<business::loader::EquipmentData> eq_data;
    if (std::filesystem::exists(vanilla_path)) {
        // Filesystem path: allows user to replace builtin data
        auto parsed = FormatDetector::parse(vanilla_path);
        ench_data = std::move(parsed.first);
        eq_data   = std::move(parsed.second);
    } else {
        // Embedded fallback: zero I/O, always available
        auto json_str = std::string{vanilla_json()};
        auto parsed = NativeJsonParser::parse_string(json_str);
        ench_data = std::move(parsed.first);
        eq_data   = std::move(parsed.second);
    }

    // Seed the tag registry from the builtin dataset's own equipment
    // categories — the vanilla fallback universe.  This is real vanilla
    // data, not synthetic: RegistryLoader::resolve no longer derives
    // `#minecraft:<category>` tags from arbitrary equipment data.
    TagRegistry base_tags;
    std::unordered_set<std::string> seen;
    for (const auto& eq : eq_data) {
        if (seen.insert(eq.category).second)
            base_tags.insert({NSID("#minecraft:" + eq.category), eq.category});
    }
    loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg, &base_tags);
}

} // namespace besq::data
