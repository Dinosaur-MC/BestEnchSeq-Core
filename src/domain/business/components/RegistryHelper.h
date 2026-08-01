#pragma once
#include "domain/business/types/Profile.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

/// Registry management: filtering, set operations, diff, validation.
///
/// Supports both chainable Builder (multi-round operations) and
/// single-shot static methods.
class RegistryHelper {
public:
    // ── Builder ───────────────────────────────────────────────────────

    RegistryHelper& load(const Profile& from);
    RegistryHelper& filter(std::function<bool(const EnchInfo&)> pred);
    RegistryHelper& filter_platform(MCE platform);
    RegistryHelper& filter_equipment(const NSID& category);
    RegistryHelper& unite(const Profile& other);
    RegistryHelper& intersect(const Profile& other);

    /// Finalise and produce the result Profile.
    Profile build(const NSID& result_name) const;

    // ── Static operations ─────────────────────────────────────────────

    /// Union: all entries from both profiles.
    static Profile unite(const NSID& name, const Profile& a, const Profile& b);

    /// Intersection: entries present in both profiles.
    static Profile intersect(const NSID& name, const Profile& a, const Profile& b);

    /// Subtract: entries in base but not in other.
    static Profile subtract(const NSID& name, const Profile& base, const Profile& other);

    /// Merge: insert_or_assign from other into base (overwrite semantics).
    /// Returns a NEW profile named `name` (base metadata preserved, other wins
    /// on enchantment conflicts; equipment/tags added if absent).
    static Profile merge(const NSID& name, const Profile& base, const Profile& other);

    /// Merge `src` into `dest` IN PLACE (overwrite semantics: src wins on
    /// enchantment conflict; equipment/tags added if absent).  Dest metadata is
    /// preserved.  Used by ProfileManager::merge.
    static void merge(Profile& dest, const Profile& src);

    /// Build a TagResolver covering the merged tag universe of an effective
    /// view: every `#tag` in `eff.tags()` is registered.  Member data is pulled
    /// from the first source profile whose attached resolver defines the tag
    /// (lowest-priority source wins, mirroring "tags: add if absent"); sources
    /// without a resolver yield an empty member set.  Used by
    /// ProfileManager::resolve_effective so the merged view is `tags_of`-queryable.
    static std::shared_ptr<TagResolver> build_tag_resolver(
        const Profile& eff, const std::vector<const Profile*>& sources);

    // ── Diff ──────────────────────────────────────────────────────────

    struct DiffEntry {
        NSID id;
        enum Status { Added, Removed, Modified } status;
    };
    struct DiffResult {
        std::vector<DiffEntry> enchantments;
        std::vector<DiffEntry> equipment;
        std::vector<DiffEntry> tags;
    };

    static DiffResult diff(const Profile& a, const Profile& b);

    // ── Validation ────────────────────────────────────────────────────

    static bool validate(const Profile& profile);

private:
    std::optional<EnchantmentRegistry> _ench;
    std::optional<EquipmentRegistry> _eq;
    std::optional<TagRegistry> _tags;
};

// ── Operator overloads ────────────────────────────────────────────────

Profile operator|(const Profile& a, const Profile& b);  // unite
Profile operator&(const Profile& a, const Profile& b);  // intersect
Profile operator+(const Profile& a, const Profile& b);  // merge (overwrite)
Profile operator-(const Profile& a, const Profile& b);  // subtract
