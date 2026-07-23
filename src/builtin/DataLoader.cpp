#include "DataLoader.h"
#include "EmbeddedData.h"
#include "domain/interface/parsers/EnchInfoParser.h"
#include "domain/orchestration/components/RawTypeAdapter.h"
#include <filesystem>

namespace besq::data {

void load_builtin_data(
    EquipmentTagRegistry& tag_reg,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    const std::filesystem::path& data_dir
) {
    auto vanilla_path = data_dir / "vanilla.json";

    if (std::filesystem::exists(vanilla_path)) {
        // Filesystem path: allows user to replace builtin data
        auto [raw_ench, raw_eq] = EnchInfoParser::parse(vanilla_path);
        RawTypeAdapter::resolve(raw_ench, raw_eq, tag_reg, eq_reg, ench_reg);
    } else {
        // Embedded fallback: zero I/O, always available
        auto json = std::string{vanilla_json()};
        auto [raw_ench, raw_eq] = EnchInfoParser::parse_native_json_str(json);
        RawTypeAdapter::resolve(raw_ench, raw_eq, tag_reg, eq_reg, ench_reg);
    }
}

} // namespace besq::data
