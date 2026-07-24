#pragma once
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "common/CommonTypes.h"
#include "common/io/json.h"
#include <chrono>
#include <string>
#include <string_view>

// ── Profile Metadata ──────────────────────────────────────────────────

struct ProfileMetadata {
    NSID name;
    std::string description;
    std::string author;
    std::string version;
    std::string parent;                          ///< Branch source profile name
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;

    // JSON keys (matching vanilla.json structure)
    static constexpr std::string_view KEY_NAME        = "name";
    static constexpr std::string_view KEY_DESCRIPTION = "description";
    static constexpr std::string_view KEY_AUTHOR      = "author";
    static constexpr std::string_view KEY_VERSION     = "version";
    static constexpr std::string_view KEY_ENCHANTMENTS = "enchantments";
    static constexpr std::string_view KEY_EQUIPMENTS   = "equipments";
    static constexpr std::string_view KEY_TAGS         = "tags";
};

// ── Profile — Business Domain First-Class Citizen ─────────────────────
//
// Profile is the primary unit of business logic. All normal operations
// take and return Profile. Cross-registry and cross-profile operations
// must be done through Profile proxy methods.

class ProfileManager;
class RegistryManager;

class Profile {
public:
    Profile() = default;
    explicit Profile(NSID name);

    // -- Metadata -------------------------------------------------------

    const ProfileMetadata& metadata() const noexcept { return _meta; }
    const NSID& name() const noexcept { return _meta.name; }
    void set_description(std::string desc) { _meta.description = std::move(desc); }
    void set_version(std::string version) { _meta.version = std::move(version); }

    // -- Registry read access (lenient, for trusted downstream) ---------

    /// Enchantment registry (for CompactAdapter, OutputFormatter, etc.)
    const EnchantmentRegistry& ench() const noexcept { return _ench; }
    /// Equipment registry
    const EquipmentRegistry& eq() const noexcept { return _eq; }
    /// Equipment tag registry
    const EquipmentTagRegistry& tags() const noexcept { return _tags; }

    // -- Profile proxy queries (preferred over manual registry access) --

    /// Are two enchantments mutually exclusive?
    bool is_compatible(const NSID& a, const NSID& b) const;

    /// Find equipment applicable for this enchantment.
    std::vector<Equipment> applicable_equipment(const NSID& ench_id) const;

    /// Does this enchantment exist in the profile?
    bool has_enchantment(const NSID& id) const;

    /// Does this equipment exist in the profile?
    bool has_equipment(const NSID& id) const;

    // -- Profile proxy mutation (controlled entry points) ---------------

    bool add_enchantment(const EnchInfo& info);
    bool update_enchantment(const EnchInfo& info);
    bool remove_enchantment(const NSID& id);
    bool add_equipment(const Equipment& eq);
    bool remove_equipment(const NSID& id);
    bool add_tag(const EquipmentTag& tag);
    bool remove_tag(const NSID& id);

    // -- Validation -----------------------------------------------------

    bool validate() const;

    // -- Clone ----------------------------------------------------------

    /// Deep copy with new name (supports snapshot/branch).
    Profile clone(const NSID& new_name) const;

    // -- Serialization --------------------------------------------------

    Json to_json() const;
    static Profile from_json(const Json& json);

private:
    friend class ProfileManager;
    friend class RegistryManager;
    friend class ProfileLoader;

    ProfileMetadata _meta;
    EnchantmentRegistry _ench;
    EquipmentRegistry _eq;
    EquipmentTagRegistry _tags;
};

// ── Json streaming operators (under global ns, found via ADL on Json) ─

Json& operator<<(Json& json, const ProfileMetadata& meta);
const Json& operator>>(const Json& json, ProfileMetadata& meta);

Json& operator<<(Json& json, const Profile& profile);
const Json& operator>>(const Json& json, Profile& profile);
