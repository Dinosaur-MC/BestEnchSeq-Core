#include "DataLoader.h"
#include "EmbeddedData.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/parsers/NativeJsonParser.h"
#include "common/io/json.h"
#include <filesystem>

namespace besq::data {

void load_builtin_data(
    EquipmentTagRegistry& tag_reg,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    const std::filesystem::path& data_dir
) {
    auto vanilla_path = data_dir / "vanilla.json";
    RegistryLoader loader;

    if (std::filesystem::exists(vanilla_path)) {
        // Filesystem path: allows user to replace builtin data
        auto [ench_data, eq_data] = FormatDetector::parse(vanilla_path);
        loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg);
    } else {
        // Embedded fallback: zero I/O, always available
        auto json_str = std::string{vanilla_json()};
        auto [ench_data, eq_data] = NativeJsonParser::parse_string(json_str);
        loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg);
    }
}

} // namespace besq::data
