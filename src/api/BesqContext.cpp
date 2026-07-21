#include "besq/besq.h"
#include "api/ProfileSet.h"
#include "api/SolvePipeline.h"
#include "parsers/EnchInfoParser.h"
#include "adapters/EnchSerializer.h"
#include "adapters/RawTypeAdapter.h"
#include "resolvers/ItemResolver.h"
#include "plugin/PluginLoader.h"
#include "registries/AlgorithmRegistration.h"

#include <filesystem>
#include <string>
#include <vector>

// ====================================================================
// pImpl definition
// ====================================================================
struct BesqContext::Impl {
    ProfileSet profiles;
};

// ====================================================================
// Lifecycle
// ====================================================================

BesqContext::BesqContext()
    : _impl(std::make_unique<Impl>())
{
}

BesqContext::~BesqContext() = default;

BesqContext::BesqContext(BesqContext&&) noexcept = default;
BesqContext& BesqContext::operator=(BesqContext&&) noexcept = default;

// ====================================================================
// Profile lifecycle
// ====================================================================

void BesqContext::load_builtin() {
    _impl->profiles.load_builtin();
}

void BesqContext::load_file(const std::string& path) {
    auto& profile = _impl->profiles.active();

    // Parse the file (auto-detects JSON, CSV, or MC official format)
    auto [raw_ench, raw_eq] = EnchInfoParser::parse(path);

    // Resolve raw enchantments into domain EnchInfo objects
    auto ench_infos = RawTypeAdapter::resolve_ench_info(raw_ench, profile.cat_reg);
    for (auto& info : ench_infos) {
        if (profile.ench_reg.get_id(info.name_id) < 0) {
            profile.ench_reg.add(info);
        }
    }

    // Resolve raw equipment into domain Equipment objects
    auto equipments = RawTypeAdapter::resolve_equipment(raw_eq, profile.cat_reg);
    for (auto& eq : equipments) {
        if (profile.eq_reg.get_id(eq.name_id) < 0) {
            profile.eq_reg.add(eq);
        }
    }
}

void BesqContext::load_data(const std::vector<std::string>& filters) {
    for (const auto& filter : filters) {
        if (std::filesystem::exists(filter)) {
            load_file(filter);
        }
        // Non-existent entries are silently skipped (future: profile-based lookup)
    }
}

// ====================================================================
// Profile management
// ====================================================================

const std::string& BesqContext::active_profile() const noexcept {
    return _impl->profiles.active_name();
}

std::vector<std::string> BesqContext::list_profiles() const {
    return _impl->profiles.list();
}

void BesqContext::activate_profile(const std::string& name) {
    _impl->profiles.activate(name);
}

void BesqContext::fork_profile(const std::string& source,
                               const std::string& dest) {
    _impl->profiles.fork(source, dest);
}

void BesqContext::merge_profile(const std::string& source,
                                const std::string& dest) {
    _impl->profiles.merge(source, dest);
}

void BesqContext::remove_profile(const std::string& name) {
    _impl->profiles.remove(name);
}

// ====================================================================
// Registry editing (active profile)
// ====================================================================

bool BesqContext::add_enchantment(const EnchInfo& info) {
    return _impl->profiles.active().ench_reg.add(info);
}

bool BesqContext::remove_enchantment(const std::string& name_id) {
    return _impl->profiles.active().ench_reg.remove(name_id);
}

bool BesqContext::modify_enchantment(const std::string& name_id,
                                     const EnchInfo& patch) {
    return _impl->profiles.active().ench_reg.modify(name_id, patch);
}

bool BesqContext::add_equipment(const Equipment& eq) {
    return _impl->profiles.active().eq_reg.add(eq);
}

bool BesqContext::remove_equipment(const std::string& name_id) {
    return _impl->profiles.active().eq_reg.remove(name_id);
}

int32_t BesqContext::add_category(const std::string& name) {
    return _impl->profiles.active().cat_reg.add(name);
}

// ====================================================================
// Registry access (active profile, read-only)
// ====================================================================

const EnchantmentRegistry& BesqContext::enchantments() const noexcept {
    return _impl->profiles.active().ench_reg;
}

const EquipmentRegistry& BesqContext::equipment() const noexcept {
    return _impl->profiles.active().eq_reg;
}

const EquipmentCategoryRegistry& BesqContext::categories() const noexcept {
    return _impl->profiles.active().cat_reg;
}

// ====================================================================
// Persistence
// ====================================================================

bool BesqContext::export_registry(const std::string& path) const {
    auto& profile = _impl->profiles.active();
    auto ext = std::filesystem::path(path).extension().string();

    if (ext == ".csv" || ext == ".CSV") {
        return EnchSerializer::export_csv(path, profile.ench_reg,
                                           profile.eq_reg, profile.cat_reg);
    }
    return EnchSerializer::export_json(path, profile.ench_reg,
                                        profile.eq_reg, profile.cat_reg);
}

// ====================================================================
// Solve
// ====================================================================

size_t BesqContext::load_plugins(const std::string& dir_path) {
    return PluginLoader::instance().load_directory(dir_path);
}

SolveResult BesqContext::solve(const SolveInput& input) {
    auto& profile = _impl->profiles.active();
    return detail::SolvePipeline::run(input, profile.ench_reg,
                                       profile.eq_reg, profile.cat_reg);
}

