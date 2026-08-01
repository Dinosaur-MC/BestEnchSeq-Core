#pragma once
#include "domain/business/types/Profile.h"

#include <functional>
#include <optional>
#include <vector>

/// Registry management: filtering, set operations, diff, validation.
///
/// Supports both chainable Builder (multi-round operations) and
/// single-shot static methods.
class RegistryManager {
public:
    // ── Builder ───────────────────────────────────────────────────────

    RegistryManager& load(const Profile& from);
    RegistryManager& filter(std::function<bool(const EnchInfo&)> pred);
    RegistryManager& filter_platform(MCE platform);
    RegistryManager& filter_equipment(const NSID& category);
    RegistryManager& unite(const Profile& other);
    RegistryManager& intersect(const Profile& other);

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
    static Profile merge(const NSID& name, const Profile& base, const Profile& other);

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
