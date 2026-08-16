#include "AutoLoadPipeline.h"
#include "common/i18n/Language.h"
#include "common/log/log.hpp"
#include "common/utils/ExeDir.hpp"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/orchestration/pipelines/ManagePipeline.h"
#include "domain/orchestration/types/ManageRequest.h"

namespace {

/// Exe-relative default for one resource kind; empty exe_dir() → CWD-relative
/// fallback keeps the pre-exe-dir behavior intact (never crashes).
std::filesystem::path exe_default(std::filesystem::path fallback) {
    const auto exe = exe_dir();
    return exe.empty() ? std::move(fallback) : exe / fallback;
}

} // namespace

AutoLoadResult AutoLoadPipeline::run(
    ProfileManager& profiles,
    ProfileLoader& loader,
    algorithm::AlgorithmLoader& algo_loader,
    const AutoLoadRequest& request)
{
    AutoLoadResult result;

    // ── 1. Built-in data FIRST (the dependency root of every profile). ──
    //    Delegates to ManagePipeline so profile semantics stay in one place.
    {
        ManageRequest req;
        req.action = ManageRequest::Action::LoadBuiltin;
        result.builtin_loaded = ManagePipeline::run(profiles, loader, req).success;
    }

    // ── 2. External profiles (native JSON/CSV/datapack). Same-key files
    //    REPLACE the old profile (load_directory's replace-on-conflict). ──
    {
        ManageRequest req;
        req.action = ManageRequest::Action::LoadDirectory;
        req.dir_path = request.profiles_dir.empty()
                           ? exe_default("profiles").string()
                           : request.profiles_dir.string();
        ManagePipeline::run(profiles, loader, req);
        result.profiles = profiles.list().size();
    }

    // ── 3. External algorithm plugins. Built-ins were registered by the
    //    AlgorithmLoader constructor; a later plugin with the same name
    //    REPLACES the earlier registration (new version wins). Missing
    //    directory → silent no-op (0) — the directory check happens HERE so
    //    auto-load never logs the scan_and_load WARN (explicit --algo-dir
    //    calls still do). ──
    const std::filesystem::path algo_dir = request.algorithms_dir.empty()
                                               ? exe_default("algorithms")
                                               : request.algorithms_dir;
    if (std::filesystem::is_directory(algo_dir))
        result.algorithms = algo_loader.scan_and_load(algo_dir.string());

    // ── 4. On-disk languages: pre-register every `{code}.json` (SET UNION
    //    with the embedded tables; a conflicting key is overwritten by the
    //    on-disk value). Missing directory → silent no-op (0). ──
    {
        auto& lang_mgr = LanguageManager::instance();
        const std::filesystem::path dir = request.langs_dir.empty()
                                              ? exe_default("langs")
                                              : request.langs_dir;
        lang_mgr.set_langs_dir(dir);
        result.langs = lang_mgr.load_all_from_disk();
    }

    LOG_INFO("auto-load: builtin=%s profiles=%zu algorithms=%zu langs=%zu",
             result.builtin_loaded ? "yes" : "no", result.profiles,
             result.algorithms, result.langs);
    return result;
}
