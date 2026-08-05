#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "domain/orchestration/orchestration.h"
#include "domain/algorithm/types/AlgorithmState.h"

namespace algorithm {
class IExecutor;
}

// ── Algorithm detail / unload (API value types) ────────────────────────
enum class AlgorithmOrigin { builtin, plugin };

struct AlgorithmDetail {
    std::string name;
    std::string version;
    AlgorithmOrigin origin = AlgorithmOrigin::builtin;
    std::string plugin_path;      ///< plugin 才有
    bool is_resumable = false;
    std::string supported_mode;   ///< "direct" / "inventory"
    bool has_audit = false;
};

/// Named-profile metadata snapshot (profile_metadata 返回）。
struct ProfileMeta {
    std::string name;
    std::vector<std::string> dependencies;
    std::string version;      ///< 空 = 未发布
    std::string release_tag;  ///< 空 = 未发布
    bool is_root = false;
    size_t ench_count = 0, eq_count = 0, tag_count = 0;
    std::string format;       ///< native_json / csv / datapack / builtin
};

/// Main public API class for BestEnchSeq.
/// Session facade: holds session state (active profile, algorithm loader) and
/// exposes service interfaces.  Management / export / formatting logic is
/// delegated to the orchestration pipelines (ManagePipeline / ExportPipeline);
/// solving, read-only registry access, and algorithm queries stay direct.
class BesqContext {
public:
    BesqContext();
    ~BesqContext();

    BesqContext(const BesqContext&) = delete;
    BesqContext& operator=(const BesqContext&) = delete;
    BesqContext(BesqContext&&) noexcept;
    BesqContext& operator=(BesqContext&&) noexcept;

    // ── Profile lifecycle ──
    void load_builtin();
    size_t load_algorithms(const std::string& dir_path);
    void load_file(const std::string& path);
    void load_data(const std::vector<std::string>& filters);

    /// Override the default profiles directory (default: `<cwd>/profiles`).
    void set_profiles_dir(const std::string& dir);

    /// Scan the profiles directory (set_profiles_dir, or `<cwd>/profiles`) and
    /// load every native JSON/CSV profile into the manager.  No-op if the
    /// directory does not exist.
    void load_profiles();

    // ── Profile management ──
    std::string active_profile() const noexcept;
    std::vector<std::string> list_profiles() const;
    void activate_profile(const std::string& name);
    void fork_profile(const std::string& source, const std::string& dest);
    void merge_profile(const std::string& source, const std::string& dest);
    void remove_profile(const std::string& name);

    /// Versioned publish: flatten the profile's effective view (incl. deps)
    /// into a self-contained profile file (embeds version/tag).  Delegates to
    /// ProfileManager::publish.  Returns false if the profile is unknown or the
    /// file cannot be written.
    bool
    publish_profile(const std::string& name, const std::string& version, const std::string& tag, const std::string& out_path);

    // ── Profile data access (by name, no activation side effect) ──
    /// The named profile's OWN data (raw registries; inherited dependency
    /// content is NOT merged in). Throws std::runtime_error when the profile
    /// is unknown. Does NOT change the active profile. Callers needing the
    /// dependency-merged view should use effective_profile().
    const Profile& profile(const std::string& name) const;

    /// Dependency-merged effective view of the named profile (mirrors what
    /// solve consumes: ProfileManager::resolve_effective). Throws
    /// std::runtime_error on unknown profile / dependency cycle. Reference is
    /// stable while the manager is not mutated — controllers must hold the
    /// context gate.
    const Profile& effective_profile(const std::string& name) const;

    // ── Registry editing (named profile) ──
    /// Stable by-name CRUD forwarded to ProfileManager's validated `_mutate`
    /// path (real-time validation + snapshot/undo). Operates on ANY profile,
    /// not just the active one.
    bool add_enchantment_to(const std::string& profile, const EnchInfo& info);
    bool remove_enchantment_from(const std::string& profile, const NSID& id);
    bool add_equipment_to(const std::string& profile, const Equipment& eq);
    bool remove_equipment_from(const std::string& profile, const NSID& id);
    bool add_tag_to(const std::string& profile, const EquipmentTag& tag);
    bool remove_tag_from(const std::string& profile, const NSID& id);
    /// Update variants — delegate to ProfileManager::update_*. Operates on ANY
    /// profile. Returns false if the profile/entry is unknown or the edit
    /// leaves the profile invalid.
    bool update_enchantment_to(const std::string& profile, const EnchInfo& patch);
    bool update_equipment_to(const std::string& profile, const Equipment& patch);
    bool update_tag_to(const std::string& profile, const EquipmentTag& patch);

    // ── Profile metadata & rename ──
    /// Existence check without throwing.
    bool profile_exists(const std::string& name) const;
    /// Snapshot the named profile's metadata + registry sizes. Unknown → throw
    /// std::runtime_error.
    ProfileMeta profile_metadata(const std::string& name) const;
    /// Rename a profile (active name follows). False if old unknown / new taken.
    bool rename_profile(const std::string& old_name, const std::string& new_name);
    /// Set a profile's dependency list. False if the profile is unknown or the
    /// change would create a dependency cycle (no mutation in either case).
    bool set_dependencies(const std::string& profile, std::vector<std::string> deps);

    // ── Registry editing (active profile) ──
    bool add_enchantment(const EnchInfo& info);
    bool remove_enchantment(const std::string& name_id);
    bool modify_enchantment(const std::string& name_id, const EnchInfo& patch);
    bool add_equipment(const Equipment& eq);
    bool remove_equipment(const std::string& name_id);
    bool add_category(const std::string& name);

    // ── Profile data import / export ──
    void import_profile(const std::string& path);
    bool export_profile(const std::string& path) const;
    /// Export the active profile's data as a string (in-memory JSON), without
    /// writing a file.  Used by `--export -` (stdout dump).
    std::string export_profile_to_string() const;

    // ── Registry access (active profile, read-only) ──
    const EnchantmentRegistry& enchantments() const noexcept;
    const EquipmentRegistry& equipment() const noexcept;
    const TagRegistry& categories() const noexcept;

    /// Live progress of the in-flight solve, read via the atomic executor
    /// handle. Idle/0.0 when no solve is running. Designed for the Web API's
    /// polling loop; thread-safe (reads the atomic shared_ptr handle).
    struct SolveProgress {
        algorithm::AlgorithmState state = algorithm::AlgorithmState::Idle;
        double progress = 0.0;
    };
    SolveProgress solve_progress() const noexcept;

    // ── Solve ──
    SolveResult solve(const SolveRequest& request);
    void abort_solve();

    // ── Format ──
    std::string format(const SolveResult& result, AlgorithmMode mode, std::string_view fmt) const;

    // ── Algorithm queries ──
    std::vector<std::string> list_algorithms() const;

    /// Per-strategy metadata (origin/version/mode/resume/audit/path).
    /// Unknown name → throw std::runtime_error.
    AlgorithmDetail algorithm_detail(const std::string& name) const;
    /// Unload a plugin by name. Built-in / unknown → false (never unloads the
    /// trusted kernel). Plugin instances must already be destroyed.
    bool unload_algorithm(const std::string& name);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
