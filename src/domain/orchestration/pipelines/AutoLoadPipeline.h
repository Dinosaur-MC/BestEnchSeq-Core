#pragma once

#include <cstddef>
#include <filesystem>

class ProfileManager;
class ProfileLoader;
namespace algorithm {
class AlgorithmLoader;
} // namespace algorithm

/// Auto-load request: which default directories to scan.  An empty path means
/// "use the exe-dir default" (profiles / algorithms / langs — see the exe-dir
/// defaults design); explicit overrides (--profile-dir / BESQ_ALGO_DIR /
/// BESQ_LANG-related env) are filled in by the caller (BesqContext::auto_load).
struct AutoLoadRequest {
    std::filesystem::path profiles_dir;    ///< empty → <exe_dir>/profiles
    std::filesystem::path algorithms_dir;  ///< empty → <exe_dir>/algorithms
    std::filesystem::path langs_dir;       ///< empty → <exe_dir>/langs
};

/// Auto-load summary (for logging / tests).
struct AutoLoadResult {
    bool builtin_loaded = false;   ///< builtin:vanilla present after load
    size_t profiles = 0;           ///< profiles loaded from the directory
    size_t algorithms = 0;         ///< algorithm plugins loaded from the directory
    size_t langs = 0;              ///< language files pre-registered from the directory
};

/// Domain-wide auto-load pipeline: the single startup entry point for both
/// CLI and GUI.  Load order is BUILT-IN FIRST, EXTERNAL SECOND — the conflict
/// rules of each resource type are already enforced by its own loader:
///   • profiles:   ProfileManager::load_directory — same key REPLACES the old
///                 profile (replace-on-conflict)
///   • algorithms: AlgorithmLoader::scan_and_load — a later plugin with the
///                 same name REPLACES the earlier registration
///                 (unregister + register; built-ins register first)
///   • langs:      LanguageManager::load_all_from_disk — SET UNION merge; a
///                 conflicting key is overwritten by the on-disk value
/// This pipeline only composes the existing domain loaders in the right
/// order; it never re-implements loading logic.
struct AutoLoadPipeline {
    static AutoLoadResult run(
        ProfileManager& profiles,
        ProfileLoader& loader,
        algorithm::AlgorithmLoader& algo_loader,
        const AutoLoadRequest& request
    );
};
