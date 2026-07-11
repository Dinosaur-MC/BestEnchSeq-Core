#pragma once

#include "types/EnchInfo.h"
#include "types/Equipment.h"
#include <filesystem>
#include <string>
#include <vector>

class EquipmentCategoryRegistry;

/// Metadata about a data pack (optional header in native JSON files).
struct EnchantmentDataPack {
    std::string name;
    std::string description;
    std::string author;
    std::string version;
};

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
