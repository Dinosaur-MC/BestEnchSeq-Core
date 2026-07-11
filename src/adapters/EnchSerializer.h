#pragma once

#include "types/EnchInfo.h"
#include "types/Equipment.h"
#include <filesystem>
#include <string>
#include <vector>

class EquipmentCategoryRegistry;

struct EnchantmentDataPack; // defined in parsers/EnchInfoParser.h

/// Serialization of domain EnchInfo / Equipment to JSON, CSV, or MC official
/// data-driven format.  Requires an EquipmentCategoryRegistry for ID-to-name
/// resolution during serialization.
///
/// Extracted from EnchInfoParser and EquipmentParser to keep parse functions
/// free of registry dependencies and adapter concerns.
struct EnchSerializer {
    // ── Enchantment serialization ─────────────────────────────────────────

    static std::string to_json(
        const std::vector<EnchInfo> &infos,
        const EquipmentCategoryRegistry &cat_reg,
        const EnchantmentDataPack *metadata = nullptr
    );

    static std::string to_csv(
        const std::vector<EnchInfo> &infos,
        const EquipmentCategoryRegistry &cat_reg
    );

    static void export_to_mc_official(
        const std::vector<EnchInfo> &infos,
        const EquipmentCategoryRegistry &cat_reg,
        const std::filesystem::path &output_dir
    );

    // ── Equipment serialization ──────────────────────────────────────────

    static std::string to_json(
        const std::vector<Equipment> &equipments,
        const EquipmentCategoryRegistry &cat_reg
    );

    static std::string to_csv(
        const std::vector<Equipment> &equipments,
        const EquipmentCategoryRegistry &cat_reg
    );
};
