#pragma once
#include "common/CommonTypes.h"
#include "common/io/json.h"
#include "common/serialization/IJsonSerializable.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class TagResolver; // fwd — Profile stores a shared_ptr; complete type only needed at call sites

// ── Profile Metadata ──────────────────────────────────────────────────

struct ProfileMetadata {
    std::string name;         ///< Identity key (string, arbitrary — B-T13)
    std::string display_name; ///< Human-friendly name for UI (optional; empty → fall back to name)
    std::string description;
    std::string author;
    std::string version;
    std::string mc_version;                ///< Minecraft release id the data targets (e.g. "26.2")
    std::string parent;                    ///< Branch source profile name
    std::vector<std::string> dependencies; ///< 声明的直接依赖（传递解析）
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;

    /// Default constructor (keeps Profile() = default valid).
    ProfileMetadata() = default;

    /// Name-only constructor: timestamps default to now.
    explicit ProfileMetadata(std::string name_)
        : name(std::move(name_)), created_at(std::chrono::system_clock::now()), updated_at(created_at) {}

    /// Full-parameter constructor.
    ProfileMetadata(std::string name_, std::string desc, std::string author_, std::string ver, std::string parent_)
        : name(std::move(name_)), description(std::move(desc)), author(std::move(author_)), version(std::move(ver)),
          parent(std::move(parent_)), created_at(std::chrono::system_clock::now()), updated_at(created_at) {}

    // JSON keys (matching vanilla.json structure)
    static constexpr std::string_view KEY_NAME = "name";
    static constexpr std::string_view KEY_DISPLAY_NAME = "display_name";
    static constexpr std::string_view KEY_DESCRIPTION = "description";
    static constexpr std::string_view KEY_AUTHOR = "author";
    static constexpr std::string_view KEY_VERSION = "version";
    static constexpr std::string_view KEY_MC_VERSION = "mc_version";
    static constexpr std::string_view KEY_PARENT = "parent";
    static constexpr std::string_view KEY_DEPENDENCIES = "dependencies";
    static constexpr std::string_view KEY_ENCHANTMENTS = "enchantments";
    static constexpr std::string_view KEY_EQUIPMENTS = "equipments";
    static constexpr std::string_view KEY_TAGS = "tags";
};

// ── Profile — Business Domain First-Class Citizen ─────────────────────
//
// Profile is the primary unit of business logic. All normal operations
// take and return Profile. Cross-registry and cross-profile operations
// must be done through Profile proxy methods.

class ProfileManager;
class RegistryHelper;

class Profile : IJsonSerializable {
public:
    Profile() = default;
    explicit Profile(std::string name);

    /// Full-parameter constructor: construct with all data upfront.
    /// Takes ownership of metadata (by move) and three registries.
    Profile(ProfileMetadata meta, EnchantmentRegistry ench, EquipmentRegistry eq, TagRegistry tags);

    // -- Metadata -------------------------------------------------------

    const ProfileMetadata& metadata() const noexcept { return _meta; }

    /// Identity key (string, arbitrary — B-T13).  Not necessarily human-readable.
    const std::string& name() const noexcept { return _meta.name; }

    /// Human-friendly name for UI.  Falls back to the identity key when no
    /// display_name is set, so it always returns something non-empty.
    std::string display_name() const { return _meta.display_name.empty() ? _meta.name : _meta.display_name; }

    /// Set the human-friendly name (empty clears it → falls back to the key).
    void set_display_name(std::string n) { _meta.display_name = std::move(n); }

    void set_description(std::string desc) { _meta.description = std::move(desc); }
    void set_version(std::string version) { _meta.version = std::move(version); }

    /// Declared direct dependencies (transitively resolved at load).
    const std::vector<std::string>& dependencies() const noexcept { return _meta.dependencies; }
    void set_dependencies(std::vector<std::string> deps) { _meta.dependencies = std::move(deps); }

    // -- Registry read access (lenient, for trusted downstream) ---------

    /// Enchantment registry (for CompactAdapter, OutputFormatter, etc.)
    const EnchantmentRegistry& ench() const noexcept { return _ench; }
    /// Equipment registry
    const EquipmentRegistry& eq() const noexcept { return _eq; }
    /// Equipment tag registry
    const TagRegistry& tags() const noexcept { return _tags; }

    // -- Tag resolver (runtime-derived; not serialized) ------------------

    /// Attach the TagResolver used at the business→algorithm boundary to
    /// compute an item's tag membership for enchantment applicability
    /// (`supported_items` ∩ `tags_of(item)`).  Populated during load; the
    /// profile JSON does not serialize it.
    void set_tag_resolver(std::shared_ptr<TagResolver> r) { _tag_resolver = std::move(r); }

    /// Accessor — returns nullptr when no resolver has been attached.
    const TagResolver* tag_resolver() const noexcept { return _tag_resolver.get(); }

    /// Shared-ownership accessor — lets callers preserve the resolver across a
    /// JSON round-trip (from_json reconstructs a fresh Profile and would
    /// otherwise drop it).
    std::shared_ptr<TagResolver> tag_resolver_ptr() const noexcept { return _tag_resolver; }

    // -- Profile proxy queries (preferred over manual registry access) --

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
    Profile clone(const std::string& new_name) const;

    // -- Serialization --------------------------------------------------

    Json to_json() const final;
    void from_json(const Json& json) final;
    static Profile from_json_static(const Json& json);

private:
    ProfileMetadata _meta;
    EnchantmentRegistry _ench;
    EquipmentRegistry _eq;
    TagRegistry _tags;
    std::shared_ptr<TagResolver> _tag_resolver;
};
