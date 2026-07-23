#pragma once
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

/// Manages discovery, loading, filtering, and resolution of multiple registry data
/// sources (enchantments + equipment).  Builtin data is always available; additional
/// sources can be registered via scan_registry_dir().
///
/// Typical usage:
///   RegistryManager mgr;
///   mgr.add_builtin();
///   if (config.registry_dir) mgr.scan_registry_dir(*config.registry_dir);
///   mgr.load_and_resolve(config.registries, cat_reg, eq_reg, ench_reg);
class RegistryManager {
  public:
    /// Register the built-in data (embedded vanilla.json, name="Vanilla").
    void add_builtin();

    /// Scan <dir> for all valid registry files/subdirectories and register
    /// each one as a source.  Each entry is loaded via EnchInfoParser::parse()
    /// (auto-detects Native JSON / Native CSV / MC Official format).
    /// Non-loadable entries are silently skipped (logged at DEBUG level).
    /// Throws std::runtime_error if <dir> does not exist or is not a directory.
    void scan_registry_dir(const std::filesystem::path &dir);

    /// Load selected/discovered registries and resolve into domain registries.
    ///
    /// filter behavior:
    ///   std::nullopt  -> load ALL discovered sources (WARN+skip on failure)
    ///   non-null      -> comma-separated list of names or paths (ALL must succeed)
    ///
    /// Path resolution: if a value is an existing file/directory on disk, it is
    /// loaded directly as a new source (name from filename stem or dir name).
    /// Otherwise the value is matched by name against already-discovered sources.
    void load_and_resolve(
        std::optional<std::string> filter, EquipmentTagRegistry &tag_reg, EquipmentRegistry &eq_reg,
        EnchantmentRegistry &ench_reg
    );

  private:
    struct Source {
        std::string name;
        std::optional<std::filesystem::path> path; // nullopt = builtin
    };
    std::vector<Source> sources_;
    bool builtin_registered_ = false;
};
