#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "domain/orchestration/orchestration.h"

namespace algorithm { class AlgorithmExecutor; }

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
    bool publish_profile(const std::string& name, const std::string& version,
                         const std::string& tag, const std::string& out_path);

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

    // ── Registry access (active profile, read-only) ──
    const EnchantmentRegistry& enchantments() const noexcept;
    const EquipmentRegistry& equipment() const noexcept;
    const TagRegistry& categories() const noexcept;

    // ── Solve ──
    SolveResult solve(const SolveRequest& request);
    void abort_solve();

    // ── Format ──
    std::string format(const SolveResult& result, AlgorithmMode mode,
                       std::string_view fmt) const;

    // ── Algorithm queries ──
    std::vector<std::string> list_algorithms() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
