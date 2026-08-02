#include "domain/interface/BesqContext.h"
#include "domain/business/ProfileManager.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/components/FormatDetector.h"
#include "domain/orchestration/components/EnchSerializer.h"
#include "domain/orchestration/components/OutputFormatter.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"

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
    algorithm::AlgorithmExecutor* active_executor{nullptr};
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
    if (!_impl->profiles.exists("builtin:vanilla")) {
        _impl->loader.load_builtin(
            _impl->profiles.create("builtin:vanilla")
        );
        _impl->profiles.activate("builtin:vanilla");
    }
}

void BesqContext::load_file(const std::string& path) {
    auto& profile = _impl->profiles.active();
    auto [ench_data, eq_data] = FormatDetector::parse(path);

    // Build temporary registries then merge into profile
    TagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RegistryLoader reg_loader;
    // Seed tag resolution with the active profile's tags (vanilla fallback).
    reg_loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg,
                       &profile.tags());

    // Merge into active profile via proxy methods
    for (const auto& [nsid, tag] : tag_reg.data())
        profile.add_tag(tag);
    for (const auto& [nsid, eq] : eq_reg.data())
        profile.add_equipment(eq);
    for (const auto& [nsid, info] : ench_reg.data())
        profile.add_enchantment(info);

    // Profile was mutated directly (bypassing manager _mutate) — the cached
    // effective view must be invalidated or a later read would be stale.
    _impl->profiles.notify_mutated();
}

void BesqContext::load_data(const std::vector<std::string>& filters) {
    for (const auto& filter : filters) {
        if (std::filesystem::exists(filter)) {
            load_file(filter);
        }
    }
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
    _impl->profiles.load_directory(dir);
}

// ====================================================================
// Profile management
// ====================================================================

const std::string& BesqContext::active_profile() const noexcept {
    static std::string cached;
    cached = _impl->profiles.active_name();
    return cached;
}

std::vector<std::string> BesqContext::list_profiles() const {
    return _impl->profiles.list();
}

void BesqContext::activate_profile(const std::string& name) {
    _impl->profiles.activate(name);
}

void BesqContext::fork_profile(const std::string& source,
                               const std::string& dest) {
    _impl->profiles.branch(source, dest);
}

void BesqContext::merge_profile(const std::string& source,
                                const std::string& dest) {
    _impl->profiles.merge(source, dest);
}

void BesqContext::remove_profile(const std::string& name) {
    _impl->profiles.remove(name);
}

bool BesqContext::publish_profile(const std::string& nsid,
                                  const std::string& version,
                                  const std::string& tag,
                                  const std::string& out_path) {
    return _impl->profiles.publish(nsid, version, tag, out_path);
}

// ====================================================================
// Registry editing (active profile)
// ====================================================================

bool BesqContext::add_enchantment(const EnchInfo& info) {
    return _impl->profiles.add_enchantment(_impl->profiles.active_name(), info);
}

bool BesqContext::remove_enchantment(const std::string& name_id) {
    return _impl->profiles.remove_enchantment(_impl->profiles.active_name(), NSID(name_id));
}

bool BesqContext::modify_enchantment(const std::string& name_id,
                                     const EnchInfo& patch) {
    auto& active = _impl->profiles.active();
    try {
        auto current = active.ench().at(NSID(name_id));
        if (patch.multiplier > 0)      current.multiplier = patch.multiplier;
        if (patch.max_level > 0)       current.max_level = patch.max_level;
        if (patch.limited_level >= 0)  current.limited_level = patch.limited_level;
        return _impl->profiles.update_enchantment(_impl->profiles.active_name(), current);
    } catch (const std::out_of_range&) {
        return false;
    }
}

bool BesqContext::add_equipment(const Equipment& eq) {
    return _impl->profiles.add_equipment(_impl->profiles.active_name(), eq);
}

bool BesqContext::remove_equipment(const std::string& name_id) {
    return _impl->profiles.remove_equipment(_impl->profiles.active_name(), NSID(name_id));
}

bool BesqContext::add_category(const std::string& name) {
    auto& active = _impl->profiles.active();
    NSID cat_nsid("#minecraft:" + name);
    if (active.tags().contains(cat_nsid))
        return false;
    return _impl->profiles.add_tag(_impl->profiles.active_name(), EquipmentTag{cat_nsid, name});
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
// Persistence
// ====================================================================

bool BesqContext::export_registry(const std::string& path) const {
    auto& profile = _impl->profiles.resolve_effective(_impl->profiles.active_name());
    auto ext = std::filesystem::path(path).extension().string();

    if (ext == ".csv" || ext == ".CSV") {
        return EnchSerializer::export_csv(path, profile);
    }
    return EnchSerializer::export_json(path, profile);
}

// ====================================================================
// Registry import
// ====================================================================

void BesqContext::import_registry(const std::string& path) {
    auto& profile = _impl->profiles.active();
    auto [ench_data, eq_data] = FormatDetector::parse(path);

    TagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RegistryLoader reg_loader;
    // Seed with the active profile's tag universe (vanilla fallback) so the
    // imported file's `#tag` supported_items references cross-validate (T10).
    reg_loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg,
                       &profile.tags());

    for (const auto& [nsid, tag] : tag_reg.data())
        profile.add_tag(tag);
    for (const auto& [nsid, eq] : eq_reg.data())
        profile.add_equipment(eq);
    for (const auto& [nsid, info] : ench_reg.data())
        profile.add_enchantment(info);

    // Direct profile mutation bypasses manager _mutate — invalidate the
    // effective-view cache so the next read reflects the imported content.
    _impl->profiles.notify_mutated();
}

void BesqContext::import_registries(const std::vector<std::string>& paths) {
    for (const auto& path : paths)
        import_registry(path);
}

// ====================================================================
// Output formatting
// ====================================================================

std::string BesqContext::format(const SolveResult& result, AlgorithmMode mode,
                                std::string_view fmt) const {
    auto& profile = _impl->profiles.resolve_effective(_impl->profiles.active_name());
    if (fmt == "json")
        return OutputFormatter::format_json(result.solutions, profile, mode);
    if (fmt == "compact")
        return OutputFormatter::format_compact(result.solutions, profile, mode);
    return OutputFormatter::format_verbose(result.solutions, profile, mode);
}

std::string BesqContext::format_verbose(const SolveResult& result, AlgorithmMode mode) const {
    return OutputFormatter::format_verbose(
        result.solutions, _impl->profiles.resolve_effective(_impl->profiles.active_name()), mode);
}

std::string BesqContext::format_compact(const SolveResult& result, AlgorithmMode mode) const {
    return OutputFormatter::format_compact(
        result.solutions, _impl->profiles.resolve_effective(_impl->profiles.active_name()), mode);
}

std::string BesqContext::format_json(const SolveResult& result, AlgorithmMode mode) const {
    return OutputFormatter::format_json(
        result.solutions, _impl->profiles.resolve_effective(_impl->profiles.active_name()), mode);
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
    _impl->active_executor = nullptr;
    auto result = SolvePipeline::run(profile, request, _impl->algo_loader,
                                     &_impl->active_executor);
    _impl->active_executor = nullptr;
    return result;
}

void BesqContext::abort_solve() {
    if (auto* exec = _impl->active_executor) {
        exec->cancel();
    }
}
