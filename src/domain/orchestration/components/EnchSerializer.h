#pragma once

#include "domain/business/types/Enchantment.h"
#include "domain/business/types/Equipment.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class TagRegistry;

#include "domain/business/types/EnchantmentDataPack.h"
#include "domain/business/types/Profile.h"

/// Serialization of domain EnchInfo / Equipment to JSON, CSV, or MC official
/// data-driven format.  Requires a TagRegistry for ID-to-name
/// resolution during serialization.
///
/// Extracted from EnchInfoParser and EquipmentParser to keep parse functions
/// free of registry dependencies and adapter concerns.
struct EnchSerializer {
    // ── Enchantment serialization ─────────────────────────────────────────

    static std::string to_json(
        const std::vector<EnchInfo> &infos,
        const TagRegistry &cat_reg,
        const EnchantmentDataPack *metadata = nullptr
    );

    static std::string to_csv(
        const std::vector<EnchInfo> &infos,
        const TagRegistry &cat_reg
    );

    static void export_to_mc_official(
        const std::vector<EnchInfo> &infos,
        const TagRegistry &cat_reg,
        const std::filesystem::path &output_dir
    );

    /// Serialize enchantments to MC official format in memory.
    /// Returns map of relative data-pack path → JSON content string.
    static std::unordered_map<std::string, std::string> to_mc_official_strings(
        const std::vector<EnchInfo> &infos,
        const TagRegistry &cat_reg
    );

    // ── Equipment serialization ──────────────────────────────────────────

    static std::string to_json(
        const std::vector<Equipment> &equipments,
        const TagRegistry &cat_reg
    );

    static std::string to_csv(
        const std::vector<Equipment> &equipments,
        const TagRegistry &cat_reg
    );

    // ── Profile-aware export ──────────────────────────────────────────

    static bool export_json(
        const std::string& path,
        const Profile& profile
    );

    static bool export_csv(
        const std::string& path,
        const Profile& profile
    );

    // ── Profile-aware export ──────────────────────────────────────────

    static void export_to_mc_official(
        const std::filesystem::path& output_dir,
        const Profile& profile
    );

    // ── Profile-aware serialization overloads ─────────────────────────

    static std::string to_json(
        const std::vector<EnchInfo>& infos,
        const Profile& profile,
        const EnchantmentDataPack* metadata = nullptr
    );
    static std::string to_csv(
        const std::vector<EnchInfo>& infos,
        const Profile& profile
    );
    static std::string to_json(
        const std::vector<Equipment>& equipments,
        const Profile& profile
    );
    static std::string to_csv(
        const std::vector<Equipment>& equipments,
        const Profile& profile
    );

    /// Profile-aware overload: return map of path → JSON content.
    static std::unordered_map<std::string, std::string> to_mc_official_strings(
        const std::vector<EnchInfo>& infos,
        const Profile& profile
    );
};
