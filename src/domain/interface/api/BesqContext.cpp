#include "besq/besq.h"
#include "ProfileSet.h"
#include "SolvePipeline.h"
#include "domain/interface/parsers/EnchInfoParser.h"
#include "domain/orchestration/components/EnchSerializer.h"
#include "domain/orchestration/components/RawTypeAdapter.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"

#include <filesystem>
#include <string>
#include <vector>

// ====================================================================
// pImpl definition
// ====================================================================
struct BesqContext::Impl {
    ProfileSet profiles;
    algorithm::AlgorithmLoader algo_loader;
};

// ====================================================================
// Lifecycle
// ====================================================================

BesqContext::BesqContext()
    : _impl(std::make_unique<Impl>())
{
    // Register all compiled-in strategies so they are immediately
    // available via solve() / list_algorithms().
    _impl->algo_loader.load_builtin();
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
        if (profile.ench_reg.index(info.id) == IRegistry<EnchInfo>::nops) {
            profile.ench_reg.insert(info);
        }
    }

    // Resolve raw equipment into domain Equipment objects
    auto equipments = RawTypeAdapter::resolve_equipment(raw_eq, profile.cat_reg);
    for (auto& eq : equipments) {
        if (profile.eq_reg.index(eq.id) == IRegistry<Equipment>::nops) {
            profile.eq_reg.insert(eq);
        }
    }
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
    return _impl->profiles.active().ench_reg.insert(info);
}

bool BesqContext::remove_enchantment(const std::string& name_id) {
    return _impl->profiles.active().ench_reg.remove(NSID(name_id));
}

bool BesqContext::modify_enchantment(const std::string& name_id,
                                     const EnchInfo& patch) {
    auto& ench_reg = _impl->profiles.active().ench_reg;
    try {
        auto current = ench_reg.get(NSID(name_id));
        if (patch.multiplier > 0)      current.multiplier = patch.multiplier;
        if (patch.max_level > 0)       current.max_level = patch.max_level;
        if (patch.limited_level >= 0)  current.limited_level = patch.limited_level;
        return ench_reg.update(current);
    } catch (const std::out_of_range&) {
        return false;
    }
}

bool BesqContext::add_equipment(const Equipment& eq) {
    return _impl->profiles.active().eq_reg.insert(eq);
}

bool BesqContext::remove_equipment(const std::string& name_id) {
    return _impl->profiles.active().eq_reg.remove(NSID(name_id));
}

int32_t BesqContext::add_category(const std::string& name) {
    auto& tag_reg = _impl->profiles.active().cat_reg;
    NSID cat_nsid("#minecraft:" + name);
    auto idx = tag_reg.index(cat_nsid);
    if (idx != IRegistry<EquipmentTag>::nops)
        return static_cast<int32_t>(idx);
    tag_reg.insert({cat_nsid, name});
    return static_cast<int32_t>(tag_reg.size() - 1);
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

const EquipmentTagRegistry& BesqContext::categories() const noexcept {
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
// Algorithm loading & solving
// ====================================================================

size_t BesqContext::load_algorithms(const std::string& dir_path) {
    return _impl->algo_loader.scan_and_load(dir_path);
}

std::vector<std::string> BesqContext::list_algorithms() const {
    return _impl->algo_loader.list();
}

SolveResult BesqContext::solve(const SolveInput& input) {
    auto& profile = _impl->profiles.active();
    return detail::SolvePipeline::run(input, _impl->algo_loader,
                                       profile.ench_reg,
                                       profile.eq_reg,
                                       profile.cat_reg);
}
