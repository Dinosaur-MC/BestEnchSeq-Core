#include "data/DataLoader.h"
#include "data/EmbeddedData.h"
#include "parsers/EnchInfoParser.h"
#include "parsers/EquipmentParser.h"
#include "adapters/RegistryResolver.h"
#include <filesystem>
#include <stdexcept>

namespace besq::data {

void load_builtin_data(
    TagResolver& tag_resolver,
    EquipmentCategoryRegistry& cat_reg,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    const std::filesystem::path& data_dir
) {
    auto vanilla_path = data_dir / "vanilla.json";

    if (std::filesystem::exists(vanilla_path)) {
        // Filesystem path: allows user to replace builtin data
        auto raw_ench = EnchInfoParser::parse(vanilla_path, tag_resolver);
        auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, cat_reg);
        ench_reg.initialize(ench_infos);

        auto raw_eq = EquipmentParser::parse(vanilla_path, tag_resolver);
        auto equipments = RegistryResolver::resolve_equipment(raw_eq, cat_reg);
        eq_reg.initialize(equipments);
    } else {
        // Embedded fallback: zero I/O, always available
        auto json = std::string{vanilla_json()};
        auto raw_ench = EnchInfoParser::parse_native_json_str(json, tag_resolver);
        auto ench_infos = RegistryResolver::resolve_ench_info(raw_ench, cat_reg);
        ench_reg.initialize(ench_infos);

        auto raw_eq = EquipmentParser::parse_native_json_str(json, tag_resolver);
        auto equipments = RegistryResolver::resolve_equipment(raw_eq, cat_reg);
        eq_reg.initialize(equipments);
    }
}

} // namespace besq::data
