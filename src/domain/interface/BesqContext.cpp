#include "domain/interface/BesqContext.h"
#include "domain/algorithm/IExecutor.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/ProfileManager.h"
#include "domain/orchestration/pipelines/ExportPipeline.h"
#include "domain/orchestration/pipelines/ManagePipeline.h"
#include "domain/orchestration/pipelines/SolvePipeline.h"
#include "domain/orchestration/types/ExportRequest.h"
#include "domain/orchestration/types/ManageRequest.h"

#include "common/i18n/Language.h"
#include <atomic>
#include <filesystem>
#include <string>
#include <utility>
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
    std::atomic<std::shared_ptr<algorithm::IExecutor>> active_executor{nullptr};
    std::string profiles_dir; ///< overridden profiles dir ("" → default `<cwd>/profiles`)
};

// ====================================================================
// Lifecycle
// ====================================================================

BesqContext::BesqContext() : _impl(std::make_unique<Impl>()) {
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
    std::filesystem::path dir = _impl->profiles_dir.empty() ? (std::filesystem::current_path() / "profiles")
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

void BesqContext::fork_profile(const std::string& source, const std::string& dest) {
    ManageRequest req;
    req.action = ManageRequest::Action::ForkProfile;
    req.source_name = source;
    req.dest_name = dest;
    ManagePipeline::run(_impl->profiles, _impl->loader, req);
}

void BesqContext::merge_profile(const std::string& source, const std::string& dest) {
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
// Profile data access (by name, no activation side effect)
// ====================================================================

const Profile& BesqContext::profile(const std::string& name) const {
    if (auto* p = _impl->profiles.find(name))
        return *p;
    throw std::runtime_error(tr_fmt("cli.err.profile_not_found", name));
}

const Profile& BesqContext::effective_profile(const std::string& name) const {
    // resolve_effective itself returns an EMPTY view for an unknown profile
    // (ProfileManager.cpp:418-425) instead of throwing — the accessor's
    // contract is a hard error, so check existence first (mirrors profile()).
    if (!_impl->profiles.exists(name))
        throw std::runtime_error(tr_fmt("cli.err.profile_not_found", name));
    return _impl->profiles.resolve_effective(name);
}

// ====================================================================
// Registry editing (named profile)
// ====================================================================

bool BesqContext::add_enchantment_to(const std::string& profile, const EnchInfo& info) {
    return _impl->profiles.add_enchantment(profile, info);
}
bool BesqContext::remove_enchantment_from(const std::string& profile, const NSID& id) {
    return _impl->profiles.remove_enchantment(profile, id);
}
bool BesqContext::add_equipment_to(const std::string& profile, const Equipment& eq) {
    return _impl->profiles.add_equipment(profile, eq);
}
bool BesqContext::remove_equipment_from(const std::string& profile, const NSID& id) {
    return _impl->profiles.remove_equipment(profile, id);
}
bool BesqContext::add_tag_to(const std::string& profile, const EquipmentTag& tag) {
    return _impl->profiles.add_tag(profile, tag);
}
bool BesqContext::remove_tag_from(const std::string& profile, const NSID& id) {
    return _impl->profiles.remove_tag(profile, id);
}

// ---- Update variants (delegate to ProfileManager::update_*) ----

bool BesqContext::update_enchantment_to(const std::string& profile, const EnchInfo& patch) {
    return _impl->profiles.update_enchantment(profile, patch);
}
bool BesqContext::update_equipment_to(const std::string& profile, const Equipment& patch) {
    return _impl->profiles.update_equipment(profile, patch);
}
bool BesqContext::update_tag_to(const std::string& profile, const EquipmentTag& patch) {
    return _impl->profiles.update_tag(profile, patch);
}

// ====================================================================
// Profile metadata & rename
// ====================================================================

bool BesqContext::profile_exists(const std::string& name) const {
    return _impl->profiles.exists(name);
}

ProfileMeta BesqContext::profile_metadata(const std::string& name) const {
    const auto* p = _impl->profiles.find(name);
    if (!p)
        throw std::runtime_error(tr_fmt("cli.err.profile_not_found", name));

    ProfileMeta m;
    const auto& meta = p->metadata();
    // name 用外部请求 key：rename 只重键 _profiles，不更新 Profile 内部
    // metadata，故内部 meta.name 可能是旧身份。
    m.name = name;
    m.dependencies = meta.dependencies;
    m.version = meta.version;
    m.release_tag = ""; // Profile 无 release_tag 字段（未发布）
    m.is_root = (name == "builtin:vanilla");
    m.ench_count = p->ench().size();
    m.eq_count = p->eq().size();
    m.tag_count = p->tags().size();
    m.format = m.is_root ? "builtin" : ""; // Profile 无 source 字段；根 profile 为内建
    return m;
}

bool BesqContext::rename_profile(const std::string& old_name, const std::string& new_name) {
    return _impl->profiles.rename(old_name, new_name);
}

bool BesqContext::set_dependencies(const std::string& profile, std::vector<std::string> deps) {
    return _impl->profiles.set_dependencies(profile, std::move(deps));
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

bool BesqContext::modify_enchantment(const std::string& name_id, const EnchInfo& patch) {
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
    req.format = ExportPipeline::format_for_path(path); // `.csv` → Csv, else Json
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

std::string BesqContext::format(const SolveResult& result, AlgorithmMode mode, std::string_view fmt) const {
    auto& profile = _impl->profiles.resolve_effective(_impl->profiles.active_name());
    ExportRequest req;
    req.target = ExportRequest::TargetType::Solution;
    req.format = (fmt == "json")      ? ExportRequest::Format::Json
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

AlgorithmDetail BesqContext::algorithm_detail(const std::string& name) const {
    const auto& loader = _impl->algo_loader;
    if (!loader.contains(name))
        throw std::runtime_error(tr_fmt("pipeline.err.unknown_algo", name, ""));

    AlgorithmDetail d;
    d.name = name;
    auto pp = loader.plugin_path(name);
    if (pp) {
        d.origin = AlgorithmOrigin::plugin;
        d.plugin_path = *pp;
    } else {
        d.origin = AlgorithmOrigin::builtin;
    }
    d.has_audit = loader.get_audit_report(name) != nullptr;

    auto algo = loader.create(name);
    if (algo) {
        d.version = std::string(algo->version());
        d.is_resumable = algo->is_resumable();
        // supported_mode 是位掩码（direct|inventory）；精确 == 会丢掉 both 能力。
        const auto mode = algo->supported_mode();
        if ((mode & AlgorithmMode::direct) && (mode & AlgorithmMode::inventory))
            d.supported_mode = "both";
        else if (mode & AlgorithmMode::inventory)
            d.supported_mode = "inventory";
        else
            d.supported_mode = "direct";
    }
    return d;
}

bool BesqContext::unload_algorithm(const std::string& name) {
    auto& loader = _impl->algo_loader;
    if (!loader.contains(name))
        return false; // 未知
    if (!loader.plugin_path(name))
        return false; // 内建（可信内核）永不卸载
    loader.unload(name);
    return true;
}

SolveResult BesqContext::solve(const SolveRequest& request) {
    auto& profile = _impl->profiles.resolve_effective(_impl->profiles.active_name());
    _impl->active_executor.store(nullptr);
    auto result = SolvePipeline::run(profile, request, _impl->algo_loader, &_impl->active_executor);
    _impl->active_executor.store(nullptr);
    return result;
}

orchestration::SolveSnapshot BesqContext::solve_snapshot(const SolveRequest& request) const {
    // 有效视图缓存引用只在构建期间持有（调用方须在 _ctx_gate 内）；快照为
    // 自包含复制，构建后 solve 不再依赖 profile 数据（P0 锁攻破 §1）。
    const auto& eff = _impl->profiles.resolve_effective(_impl->profiles.active_name());
    return orchestration::build_solve_snapshot(request, eff);
}

BesqContext::SolveProgress BesqContext::solve_progress() const noexcept {
    auto exec = _impl->active_executor.load();
    if (!exec)
        return {};
    return SolveProgress{exec->state(), exec->progress()};
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

void BesqContext::pause_solve() {
    // Same handle-copy discipline as abort_solve. IExecutor::pause() on an
    // Idle/Completed/Paused executor is a safe no-op (the executor's own
    // state CAS guards it), so a pause racing the publish window simply
    // loses — the solve then runs to completion.
    auto exec = _impl->active_executor.load();
    if (exec) {
        exec->pause();
    }
}

void BesqContext::resume_solve() {
    auto exec = _impl->active_executor.load();
    if (exec) {
        exec->resume();
    }
}
