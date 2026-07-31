#include "domain/interface/BesqContext.h"
#include "domain/business/managers/ProfileManager.h"
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
    if (!_impl->profiles.exists(NSID("builtin:vanilla"))) {
        _impl->loader.load_builtin(
            _impl->profiles.create(NSID("builtin:vanilla"))
        );
        _impl->profiles.activate(NSID("builtin:vanilla"));
    }
}

void BesqContext::load_file(const std::string& path) {
    auto& profile = _impl->profiles.active();
    auto [ench_data, eq_data] = FormatDetector::parse(path);

    // Build temporary registries then merge into profile
    EquipmentTagRegistry tag_reg;
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
}

void BesqContext::load_data(const std::vector<std::string>& filters) {
    for (const auto& filter : filters) {
        if (std::filesystem::exists(filter)) {
            load_file(filter);
        }
    }
}

// ====================================================================
// Profile management
// ====================================================================

const std::string& BesqContext::active_profile() const noexcept {
    static std::string cached;
    cached = _impl->profiles.active_name().str();
    return cached;
}

std::vector<std::string> BesqContext::list_profiles() const {
    auto nsids = _impl->profiles.list();
    std::vector<std::string> names;
    names.reserve(nsids.size());
    for (const auto& nsid : nsids)
        names.push_back(nsid.str());
    return names;
}

void BesqContext::activate_profile(const std::string& name) {
    _impl->profiles.activate(NSID(name));
}

void BesqContext::fork_profile(const std::string& source,
                               const std::string& dest) {
    _impl->profiles.branch(NSID(source), NSID(dest));
}

void BesqContext::merge_profile(const std::string& source,
                                const std::string& dest) {
    _impl->profiles.merge(NSID(source), NSID(dest));
}

void BesqContext::remove_profile(const std::string& name) {
    _impl->profiles.remove(NSID(name));
}

// ====================================================================
// Registry editing (active profile)
// ====================================================================

bool BesqContext::add_enchantment(const EnchInfo& info) {
    return _impl->profiles.active().add_enchantment(info);
}

bool BesqContext::remove_enchantment(const std::string& name_id) {
    return _impl->profiles.active().remove_enchantment(NSID(name_id));
}

bool BesqContext::modify_enchantment(const std::string& name_id,
                                     const EnchInfo& patch) {
    auto& active = _impl->profiles.active();
    try {
        auto current = active.ench().at(NSID(name_id));
        if (patch.multiplier > 0)      current.multiplier = patch.multiplier;
        if (patch.max_level > 0)       current.max_level = patch.max_level;
        if (patch.limited_level >= 0)  current.limited_level = patch.limited_level;
        return active.update_enchantment(current);
    } catch (const std::out_of_range&) {
        return false;
    }
}

bool BesqContext::add_equipment(const Equipment& eq) {
    return _impl->profiles.active().add_equipment(eq);
}

bool BesqContext::remove_equipment(const std::string& name_id) {
    return _impl->profiles.active().remove_equipment(NSID(name_id));
}

bool BesqContext::add_category(const std::string& name) {
    auto& active = _impl->profiles.active();
    NSID cat_nsid("#minecraft:" + name);
    if (active.tags().contains(cat_nsid))
        return false;
    return active.add_tag({cat_nsid, name});
}

// ====================================================================
// Registry access (active profile, read-only)
// ====================================================================

const EnchantmentRegistry& BesqContext::enchantments() const noexcept {
    return _impl->profiles.active().ench();
}

const EquipmentRegistry& BesqContext::equipment() const noexcept {
    return _impl->profiles.active().eq();
}

const EquipmentTagRegistry& BesqContext::categories() const noexcept {
    return _impl->profiles.active().tags();
}

// ====================================================================
// Persistence
// ====================================================================

bool BesqContext::export_registry(const std::string& path) const {
    auto& profile = _impl->profiles.active();
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

    EquipmentTagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    RegistryLoader reg_loader;
    reg_loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg);

    for (const auto& [nsid, tag] : tag_reg.data())
        profile.add_tag(tag);
    for (const auto& [nsid, eq] : eq_reg.data())
        profile.add_equipment(eq);
    for (const auto& [nsid, info] : ench_reg.data())
        profile.add_enchantment(info);
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
    auto& profile = _impl->profiles.active();
    if (fmt == "json")
        return OutputFormatter::format_json(result.solutions, profile, mode);
    if (fmt == "compact")
        return OutputFormatter::format_compact(result.solutions, profile, mode);
    return OutputFormatter::format_verbose(result.solutions, profile, mode);
}

std::string BesqContext::format_verbose(const SolveResult& result, AlgorithmMode mode) const {
    return OutputFormatter::format_verbose(
        result.solutions, _impl->profiles.active(), mode);
}

std::string BesqContext::format_compact(const SolveResult& result, AlgorithmMode mode) const {
    return OutputFormatter::format_compact(
        result.solutions, _impl->profiles.active(), mode);
}

std::string BesqContext::format_json(const SolveResult& result, AlgorithmMode mode) const {
    return OutputFormatter::format_json(
        result.solutions, _impl->profiles.active(), mode);
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
    auto& profile = _impl->profiles.active();
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
