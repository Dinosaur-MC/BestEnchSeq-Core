#pragma once

#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/Equipment.h"
#include <filesystem>
#include <string>
#include <vector>

class EquipmentTagRegistry;

struct EnchantmentDataPack; // defined in parsers/EnchInfoParser.h

/// Serialization of domain EnchInfo / Equipment to JSON, CSV, or MC official
/// data-driven format.  Requires an EquipmentTagRegistry for ID-to-name
/// resolution during serialization.
///
/// Extracted from EnchInfoParser and EquipmentParser to keep parse functions
/// free of registry dependencies and adapter concerns.
struct EnchSerializer {
    // ── Enchantment serialization ─────────────────────────────────────────

    static std::string to_json(
        const std::vector<EnchInfo> &infos,
        const EquipmentTagRegistry &cat_reg,
        const EnchantmentDataPack *metadata = nullptr
    );

    static std::string to_csv(
        const std::vector<EnchInfo> &infos,
        const EquipmentTagRegistry &cat_reg
    );

    static void export_to_mc_official(
        const std::vector<EnchInfo> &infos,
        const EquipmentTagRegistry &cat_reg,
        const std::filesystem::path &output_dir
    );

    // ── Equipment serialization ──────────────────────────────────────────

    static std::string to_json(
        const std::vector<Equipment> &equipments,
        const EquipmentTagRegistry &cat_reg
    );

    static std::string to_csv(
        const std::vector<Equipment> &equipments,
        const EquipmentTagRegistry &cat_reg
    );

    // ── Full-registry export ────────────────────────────────────────────

    /// Export current registry state to a JSON file.
    static bool export_json(const std::string& path,
                            const EnchantmentRegistry& ench_reg,
                            const EquipmentRegistry& eq_reg,
                            const EquipmentTagRegistry& cat_reg);

    /// Export current registry state to CSV files (enchantments.csv + equipments.csv in same dir).
    static bool export_csv(const std::string& path,
                           const EnchantmentRegistry& ench_reg,
                           const EquipmentRegistry& eq_reg,
                           const EquipmentTagRegistry& cat_reg);
};
