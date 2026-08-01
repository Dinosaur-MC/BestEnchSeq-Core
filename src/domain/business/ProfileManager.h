#pragma once
#include "domain/business/types/Profile.h"

#include <memory>
#include <unordered_map>
#include <vector>

/// Manages Profile lifecycle: creation, activation, snapshot, branching, merging.
///
/// Built upon the concept of the former interface/ProfileSet, extended with
/// snapshot, branch, version metadata, and NSID-based naming.
class ProfileManager {
public:
    ProfileManager() = default;

    // ── CRUD ──────────────────────────────────────────────────────────

    /// Create an empty profile.
    Profile& create(const NSID& name);

    /// Create a profile from an existing source (deep copy).
    Profile& create_from(const NSID& source, const NSID& dest);

    /// Remove a profile. Returns false if not found.
    bool remove(const NSID& name);

    /// Check if a profile exists.
    bool exists(const NSID& name) const;

    /// Find profile by name (nullptr if not found).
    Profile* find(const NSID& name);
    const Profile* find(const NSID& name) const;

    /// List all profile names.
    std::vector<NSID> list() const;

    /// Number of managed profiles.
    size_t size() const noexcept { return _profiles.size(); }

    // ── Activation ────────────────────────────────────────────────────

    /// Set the active profile. Throws std::runtime_error if not found.
    void activate(const NSID& name);

    /// Get active profile. Throws std::runtime_error if manager is empty.
    Profile& active();
    const Profile& active() const;

    /// Name of the active profile.
    const NSID& active_name() const noexcept { return _active; }

    // ── Snapshot ──────────────────────────────────────────────────────

    /// Create an immutable point-in-time copy of a profile.
    Profile& snapshot(const NSID& source, const NSID& snapshot_name);

    // ── Branch ────────────────────────────────────────────────────────

    /// Create an independently evolvable fork of a profile.
    /// The branch inherits all data and tracks parent metadata.
    Profile& branch(const NSID& source, const NSID& branch_name);

    // ── Merge ─────────────────────────────────────────────────────────

    /// Merge source profile into dest (insert_or_assign semantics).
    /// Source entries overwrite dest entries on conflict.
    void merge(const NSID& source, const NSID& dest);

private:
    Profile* _find(const NSID& name);
    const Profile* _find(const NSID& name) const;

    std::unordered_map<NSID, std::unique_ptr<Profile>> _profiles;
    NSID _active;
};
