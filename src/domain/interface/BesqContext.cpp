#include "domain/interface/BesqContext.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/orchestration/pipelines/ManagePipeline.h"
#include "domain/orchestration/pipelines/ExportPipeline.h"
#include "domain/orchestration/pipelines/SolvePipeline.h"
#include "domain/orchestration/types/ManageRequest.h"
#include "domain/orchestration/types/ExportRequest.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

// ====================================================================
// pImpl definition
// ====================================================================
struct BesqContext::Impl {
    ProfileManager profiles;
    ProfileLoader loader;
    algorithm::AlgorithmLoader algo_loader;
    /// Shared handle to the executor owned by the solve thread's stage_execute.
    /// Atomic + shared_ptr: the solve thread stores/clears it, and an abort
    /// thread copies it — the copy keeps the executor alive through cancel(),
    /// so cancel() can never dereference a destroyed executor (B-T22).
    std::atomic<std::shared_ptr<algorithm::AlgorithmExecutor>> active_executor{nullptr};
    std::string profiles_dir;   ///< overridden profiles dir ("" → default `<cwd>/profiles`)
};

// ====================================================================
// Lifecycle
// ====================================================================

BesqContext::BesqContext()
    : _impl(std::make_unique<Impl>())
{
    _impl->algo_loader.load_builtin();
}

BesqContext::~BesqContext() = default;

BesqContext::BesqContext(BesqContext&&) noexcept = default;
BesqContext& BesqContext::operator=(BesqContext&&) noexcept = default;

// ====================================================================
// Profile lifecycle
// ====================================================================

void BesqContext::load_builtin() {
    // ManagePipeline LoadBuiltin guards on `exists("builtin:vanilla")`
    // (idempotent), then loads and activates it.
    ManageRequest req;
    req.action = ManageRequest::Action::LoadBuiltin;
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

void BesqContext::load_file(const std::string& path) {
    ManageRequest req;
    req.action = ManageRequest::Action::LoadFile;
    req.file_path = path;
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

void BesqContext::load_data(const std::vector<std::string>& filters) {
    // ManagePipeline LoadData filters on `exists()` and routes each existing
    // path through LoadFile.
    ManageRequest req;
    req.action = ManageRequest::Action::LoadData;
    req.filters = filters;
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

void BesqContext::set_profiles_dir(const std::string& dir) {
    _impl->profiles_dir = dir;
}

void BesqContext::load_profiles() {
    // Default directory is `<cwd>/profiles` (no argv available at this layer;
    // the CLI `--profile-dir` in T9 overrides via set_profiles_dir).
    std::filesystem::path dir = _impl->profiles_dir.empty()
        ? (std::filesystem::current_path() / "profiles")
        : std::filesystem::path(_impl->profiles_dir);
    ManageRequest req;
    req.action = ManageRequest::Action::LoadDirectory;
    req.dir_path = dir.string();
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

// ====================================================================
// Profile management
// ====================================================================

std::string BesqContext::active_profile() const noexcept {
    return _impl->profiles.active_name();
}

std::vector<std::string> BesqContext::list_profiles() const {
    return _impl->profiles.list();
}

void BesqContext::activate_profile(const std::string& name) {
    ManageRequest req;
    req.action = ManageRequest::Action::ActivateProfile;
    req.profile_name = name;
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

void BesqContext::fork_profile(const std::string& source,
                               const std::string& dest) {
    ManageRequest req;
    req.action = ManageRequest::Action::ForkProfile;
    req.source_name = source;
    req.dest_name = dest;
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

void BesqContext::merge_profile(const std::string& source,
                                const std::string& dest) {
    ManageRequest req;
    req.action = ManageRequest::Action::MergeProfile;
    req.source_name = source;
    req.dest_name = dest;
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

void BesqContext::remove_profile(const std::string& name) {
    ManageRequest req;
    req.action = ManageRequest::Action::RemoveProfile;
    req.profile_name = name;
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

bool BesqContext::publish_profile(const std::string& name,
                                  const std::string& version,
                                  const std::string& tag,
                                  const std::string& out_path) {
    ManageRequest req;
    req.action = ManageRequest::Action::PublishProfile;
    req.profile_name = name;
    req.publish_version = version;
    req.publish_tag = tag;
    req.output_path = out_path;
    return ManagePipeline::run(_impl->profiles, _impl->loader, req).success;
}

// ====================================================================
// Registry editing (active profile)
// ====================================================================

bool BesqContext::add_enchantment(const EnchInfo& info) {
    ManageRequest req;
    req.action = ManageRequest::Action::AddEnchantment;
    req.ench_info = info;
    return ManagePipeline::run(_impl->profiles, _impl->loader, req).success;
}

bool BesqContext::remove_enchantment(const std::string& name_id) {
    // DUAL-USE: profile_name carries the enchantment content NSID for
    // RemoveEnchantment (ManagePipeline wraps it in NSID(...)).
    ManageRequest req;
    req.action = ManageRequest::Action::RemoveEnchantment;
    req.profile_name = name_id;
    return ManagePipeline::run(_impl->profiles, _impl->loader, req).success;
}

bool BesqContext::modify_enchantment(const std::string& name_id,
                                     const EnchInfo& patch) {
    // DUAL-USE: profile_name carries the enchantment content NSID for
    // ModifyEnchantment.  ManagePipeline patches multiplier/max_level/
    // limited_level in place and catches std::out_of_range → success=false.
    ManageRequest req;
    req.action = ManageRequest::Action::ModifyEnchantment;
    req.profile_name = name_id;
    req.ench_info = patch;
    return ManagePipeline::run(_impl->profiles, _impl->loader, req).success;
}

bool BesqContext::add_equipment(const Equipment& eq) {
    ManageRequest req;
    req.action = ManageRequest::Action::AddEquipment;
    req.equip = eq;
    return ManagePipeline::run(_impl->profiles, _impl->loader, req).success;
}

bool BesqContext::remove_equipment(const std::string& name_id) {
    // DUAL-USE: profile_name carries the equipment content NSID for
    // RemoveEquipment (ManagePipeline wraps it in NSID(...)).
    ManageRequest req;
    req.action = ManageRequest::Action::RemoveEquipment;
    req.profile_name = name_id;
    return ManagePipeline::run(_impl->profiles, _impl->loader, req).success;
}

bool BesqContext::add_category(const std::string& name) {
    ManageRequest req;
    req.action = ManageRequest::Action::AddCategory;
    req.category_name = name;
    return ManagePipeline::run(_impl->profiles, _impl->loader, req).success;
}

// ====================================================================
// Profile data import / export
// ====================================================================

void BesqContext::import_profile(const std::string& path) {
    ManageRequest req;
    req.action = ManageRequest::Action::ImportRegistry;
    req.file_path = path;
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

bool BesqContext::export_profile(const std::string& path) const {
    auto& profile = _impl->profiles.resolve_effective(_impl->profiles.active_name());
    ExportRequest req;
    req.target = ExportRequest::TargetType::Registry;
    req.output_path = path;
    req.format = ExportPipeline::format_for_path(path);   // `.csv` → Csv, else Json
    return ExportPipeline::run(profile, req).success;
}

std::string BesqContext::export_profile_to_string() const {
    auto& profile = _impl->profiles.resolve_effective(_impl->profiles.active_name());
    ExportRequest req;
    req.target = ExportRequest::TargetType::Registry;
    req.format = ExportRequest::Format::Json;
    // output_path 留空 → ExportPipeline 走内存导出，返回 result.content。
    return ExportPipeline::run(profile, req).content;
}

// ====================================================================
// Registry access (active profile, read-only)
// ====================================================================

const EnchantmentRegistry& BesqContext::enchantments() const noexcept {
    return _impl->profiles.resolve_effective(_impl->profiles.active_name()).ench();
}

const EquipmentRegistry& BesqContext::equipment() const noexcept {
    return _impl->profiles.resolve_effective(_impl->profiles.active_name()).eq();
}

const TagRegistry& BesqContext::categories() const noexcept {
    return _impl->profiles.resolve_effective(_impl->profiles.active_name()).tags();
}

// ====================================================================
// Output formatting
// ====================================================================

std::string BesqContext::format(const SolveResult& result, AlgorithmMode mode,
                                std::string_view fmt) const {
    auto& profile = _impl->profiles.resolve_effective(_impl->profiles.active_name());
    ExportRequest req;
    req.target = ExportRequest::TargetType::Solution;
    req.format = (fmt == "json") ? ExportRequest::Format::Json
               : (fmt == "compact") ? ExportRequest::Format::Compact
                                    : ExportRequest::Format::Verbose;
    req.solutions = result.solutions;
    req.mode = mode;
    req.success = result.success;
    req.algorithm_used = result.algorithm_used;
    req.computation_time_ms = result.computation_time_ms;
    return ExportPipeline::run(profile, req).content;
}

// ====================================================================
// Algorithm loading & solving
// ====================================================================

size_t BesqContext::load_algorithms(const std::string& dir_path) {
    return _impl->algo_loader.scan_and_load(dir_path);
}

std::vector<std::string> BesqContext::list_algorithms() const {
    return _impl->algo_loader.list();
}

SolveResult BesqContext::solve(const SolveRequest& request) {
    auto& profile = _impl->profiles.resolve_effective(_impl->profiles.active_name());
    _impl->active_executor.store(nullptr);
    auto result = SolvePipeline::run(profile, request, _impl->algo_loader,
                                     &_impl->active_executor);
    _impl->active_executor.store(nullptr);
    return result;
}

void BesqContext::abort_solve() {
    // Copy the handle under synchronization: the shared_ptr copy keeps the
    // executor alive for the duration of cancel(), so a concurrent solve()
    // clearing the handle (and dropping its own reference) can never leave us
    // dereferencing a destroyed executor.  No-op when no solve is running or
    // after completion (cancel() on a Completed/Failed/Idle executor is a
    // no-op).
    auto exec = _impl->active_executor.load();
    if (exec) {
        exec->cancel();
    }
}
