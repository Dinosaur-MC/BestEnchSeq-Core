#include "ProfileManager.h"
#include "domain/business/components/RegistryHelper.h"
#include "domain/business/components/Serializer.h"  // Profile << Json (snapshot)
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/parsers/McOfficialParser.h"
#include "builtin/DataLoader.h"
#include "common/io/FileUtils.hpp"
#include "common/io/json.h"
#include "common/log/log.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

// ── Internal helpers ──────────────────────────────────────────────────

namespace {

/// NSID allowed character set (mirrors CommonTypes.cpp validate_id).
/// pack.mcmeta `pack.id` / directory stems may contain characters outside this
/// set (e.g. "More Enchants 1.4" contains spaces and dots) which are INVALID
/// in an NSID — replace them with `_`.  Also guard against a leading digit
/// (NSID::validate_id rejects a digit as the first character).
std::string sanitize_nsid_name(std::string raw) {
    static constexpr std::string_view valid =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-/";
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        if (valid.find(c) != std::string_view::npos)
            out.push_back(c);
        else
            out.push_back('_');
    }
    if (out.empty())
        out = "datapack";
    if (std::isdigit(static_cast<unsigned char>(out[0])))
        out.insert(out.begin(), '_');
    return out;
}

/// Read pack.mcmeta `pack.id` (if present), else the datapack directory stem —
/// both sanitized into a valid NSID name.
std::string derive_datapack_name(const std::filesystem::path& dir) {
    std::string raw = dir.filename().string();
    const auto mcmeta_path = dir / "pack.mcmeta";
    if (std::filesystem::is_regular_file(mcmeta_path)) {
        try {
            Json mcmeta = Json::parse(file_utils::read_file(mcmeta_path));
            if (mcmeta.has("pack")) {
                Json pack = mcmeta["pack"];
                if (pack.has("id"))
                    raw = pack["id"].as<std::string>();
            }
        } catch (...) {
            // Malformed pack.mcmeta → fall back to the directory stem.
        }
    }
    return sanitize_nsid_name(raw);
}

} // namespace

Profile* ProfileManager::_find(const NSID& name) {
    auto it = _profiles.find(name);
    return (it != _profiles.end()) ? it->second.get() : nullptr;
}

const Profile* ProfileManager::_find(const NSID& name) const {
    auto it = _profiles.find(name);
    return (it != _profiles.end()) ? it->second.get() : nullptr;
}

// ── CRUD ──────────────────────────────────────────────────────────────

Profile& ProfileManager::create(const NSID& name) {
    if (exists(name))
        throw std::runtime_error("Profile already exists: " + name.str());
    auto p = std::make_unique<Profile>(name);
    Profile& ref = *p;
    _profiles[name] = std::move(p);
    _effective_cache.clear();
    return ref;
}

Profile& ProfileManager::create_from(const NSID& source, const NSID& dest) {
    if (!exists(source))
        throw std::runtime_error("Source profile not found: " + source.str());
    if (exists(dest))
        throw std::runtime_error("Destination profile already exists: " + dest.str());

    const Profile& src = *find(source);
    auto p = std::make_unique<Profile>(src.clone(dest));
    Profile& ref = *p;
    _profiles[dest] = std::move(p);
    _effective_cache.clear();
    return ref;
}

bool ProfileManager::remove(const NSID& name) {
    auto it = _profiles.find(name);
    if (it == _profiles.end()) return false;

    _profiles.erase(it);
    _undo_log.erase(name);  // 清理该 profile 的 undo 历史

    // Adjust active if needed
    if (_profiles.empty()) {
        _active = NSID();
    } else if (_active == name) {
        _active = _profiles.begin()->first;
    }
    _effective_cache.clear();
    return true;
}

bool ProfileManager::exists(const NSID& name) const {
    return _profiles.find(name) != _profiles.end();
}

Profile* ProfileManager::find(const NSID& name) {
    return _find(name);
}

const Profile* ProfileManager::find(const NSID& name) const {
    return _find(name);
}

std::vector<NSID> ProfileManager::list() const {
    std::vector<NSID> names;
    names.reserve(_profiles.size());
    for (const auto& [nsid, _] : _profiles)
        names.push_back(nsid);
    return names;
}

// ── Stable CRUD (real-time validation + snapshot/undo) ────────────────

bool ProfileManager::_mutate(const NSID& profile, std::function<bool(Profile&)> op) {
    Profile* p = _find(profile);
    if (!p) return false;

    // 应用前校验（实时）：数据本身已有效才允许编辑。
    if (!RegistryHelper::validate(*p)) return false;

    // 快照（变更前状态）。
    Json before;
    before << *p;
    _undo_log[profile].push_back(Snapshot{std::move(before)});

    // 应用。
    if (!op(*p)) {
        _undo_log[profile].pop_back();
        return false;
    }

    // 事后校验：失败则回滚到快照，不留脏状态。
    if (!RegistryHelper::validate(*p)) {
        auto keep_resolver = p->tag_resolver_ptr();  // from_json 会重建 Profile，需保留 resolver
        p->from_json(_undo_log[profile].back().before);
        p->set_tag_resolver(std::move(keep_resolver));
        _undo_log[profile].pop_back();
        return false;
    }

    _effective_cache.clear();
    return true;
}

bool ProfileManager::add_enchantment(const NSID& profile, const EnchInfo& info) {
    return _mutate(profile, [&](Profile& p) { return p.add_enchantment(info); });
}

bool ProfileManager::update_enchantment(const NSID& profile, const EnchInfo& patch) {
    return _mutate(profile, [&](Profile& p) { return p.update_enchantment(patch); });
}

bool ProfileManager::remove_enchantment(const NSID& profile, const NSID& id) {
    return _mutate(profile, [&](Profile& p) { return p.remove_enchantment(id); });
}

bool ProfileManager::add_equipment(const NSID& profile, const Equipment& eq) {
    return _mutate(profile, [&](Profile& p) { return p.add_equipment(eq); });
}

bool ProfileManager::remove_equipment(const NSID& profile, const NSID& id) {
    return _mutate(profile, [&](Profile& p) { return p.remove_equipment(id); });
}

bool ProfileManager::add_tag(const NSID& profile, const EquipmentTag& tag) {
    return _mutate(profile, [&](Profile& p) { return p.add_tag(tag); });
}

bool ProfileManager::remove_tag(const NSID& profile, const NSID& id) {
    return _mutate(profile, [&](Profile& p) { return p.remove_tag(id); });
}

bool ProfileManager::undo(const NSID& profile) {
    // 防御性顺序：先确认 profile 存在，再消费快照。
    Profile* p = _find(profile);
    if (!p) return false;

    auto it = _undo_log.find(profile);
    if (it == _undo_log.end() || it->second.empty())
        return false;

    auto before = std::move(it->second.back().before);
    it->second.pop_back();

    auto keep_resolver = p->tag_resolver_ptr();  // from_json 会重建 Profile，需保留 resolver
    p->from_json(before);
    p->set_tag_resolver(std::move(keep_resolver));
    _effective_cache.clear();
    return true;
}

// ── Activation ────────────────────────────────────────────────────────

void ProfileManager::activate(const NSID& name) {
    if (!exists(name))
        throw std::runtime_error("Profile not found: " + name.str());
    _active = name;
}

Profile& ProfileManager::active() {
    if (_profiles.empty())
        throw std::runtime_error("No active profile — ProfileManager is empty");
    return *_profiles.at(_active);
}

const Profile& ProfileManager::active() const {
    if (_profiles.empty())
        throw std::runtime_error("No active profile — ProfileManager is empty");
    return *_profiles.at(_active);
}

// ── Snapshot ──────────────────────────────────────────────────────────

Profile& ProfileManager::snapshot(const NSID& source, const NSID& snapshot_name) {
    if (!exists(source))
        throw std::runtime_error("Source profile not found: " + source.str());
    if (exists(snapshot_name))
        throw std::runtime_error("Snapshot name already exists: " + snapshot_name.str());

    const Profile& src = *find(source);
    auto p = std::make_unique<Profile>(src.clone(snapshot_name));
    p->set_version("snapshot");  // mark as snapshot
    Profile& ref = *p;
    _profiles[snapshot_name] = std::move(p);
    _effective_cache.clear();
    return ref;
}

// ── Branch ────────────────────────────────────────────────────────────

Profile& ProfileManager::branch(const NSID& source, const NSID& branch_name) {
    if (!exists(source))
        throw std::runtime_error("Source profile not found: " + source.str());
    if (exists(branch_name))
        throw std::runtime_error("Branch name already exists: " + branch_name.str());

    const Profile& src = *find(source);
    auto p = std::make_unique<Profile>(src.clone(branch_name));
    Profile& ref = *p;
    _profiles[branch_name] = std::move(p);
    _effective_cache.clear();
    return ref;
}

// ── Merge ─────────────────────────────────────────────────────────────

void ProfileManager::merge(const NSID& source, const NSID& dest) {
    if (source == dest) return;  // self-merge is no-op
    const Profile& src = *find(source);
    Profile& dst = *find(dest);
    // Source wins on conflict, merged in place (dest metadata preserved).
    RegistryHelper::merge(dst, src);
    _effective_cache.clear();
}

// ── Dependency graph ──────────────────────────────────────────────────

void ProfileManager::_build_graph() const {
    _dep_graph.clear();
    for (const auto& [nsid, p] : _profiles)
        _dep_graph[nsid] = p->dependencies();
}

std::vector<NSID> ProfileManager::resolve_dependencies(const NSID& profile) const {
    // Rebuild from the current profiles so direct set_dependencies() calls
    // (which bypass the manager) are always honored.
    _build_graph();

    std::vector<NSID> order;
    std::unordered_map<NSID, uint8_t> color;   // 0 白 1 灰 2 黑
    std::function<bool(const NSID&)> dfs = [&](const NSID& n) -> bool {
        color[n] = 1;
        auto it = _dep_graph.find(n);
        if (it != _dep_graph.end()) {
            for (const auto& d : it->second) {
                if (color[d] == 1) return false;          // back edge → cycle
                if (color[d] == 0 && !dfs(d)) return false;
            }
        }
        color[n] = 2;
        order.push_back(n);
        return true;
    };
    if (_dep_graph.find(profile) == _dep_graph.end()) return {};
    if (!dfs(profile)) return {};
    order.pop_back();   // 去掉目标自身
    return order;
}

// ── Effective view (topological merge + TagResolver + cache) ──────────

const Profile& ProfileManager::resolve_effective(const NSID& profile) const {
    auto cache_it = _effective_cache.find(profile);
    if (cache_it != _effective_cache.end())
        return *cache_it->second;

    auto chain = resolve_dependencies(profile);   // 依赖在前，自身排除
    auto eff = std::make_unique<Profile>(profile); // 空 Profile 起步

    // 收集参与合并的源：依赖按拓扑序（下层在前），目标自身最后。
    std::vector<const Profile*> sources;
    for (const auto& dep : chain)
        if (const Profile* d = _find(dep))
            sources.push_back(d);
    if (const Profile* self = _find(profile))
        sources.push_back(self);

    // 依赖按拓扑序 merge（上层覆盖下层）；最后 merge 目标自身。
    for (const Profile* src : sources)
        RegistryHelper::merge(*eff, *src);

    // 构建合并后 tag 宇宙的 TagResolver 并挂到 eff。
    eff->set_tag_resolver(RegistryHelper::build_tag_resolver(*eff, sources));

    Profile& out = *eff;
    _effective_cache[profile] = std::move(eff);
    return out;
}

// ── Load directory ────────────────────────────────────────────────────

void ProfileManager::load_directory(const std::filesystem::path& dir) {
    if (!std::filesystem::is_directory(dir))
        return;

    ProfileLoader loader;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const auto& path = entry.path();
        if (entry.is_regular_file()) {
            const auto ext = path.extension();
            if (ext != ".json" && ext != ".csv")
                continue;

            Profile loaded = loader.load(path);
            const NSID name = loaded.name().empty() ? NSID(path.stem().string()) : loaded.name();
            if (exists(name))
                remove(name);  // replace-on-conflict
            _profiles[name] = std::make_unique<Profile>(std::move(loaded));
        } else if (entry.is_directory() &&
                   std::filesystem::exists(path / "pack.mcmeta")) {
            // A datapack subdirectory — load it as a profile.
            load_datapack(path);
        }
    }

    // The vanilla base profile must exist for dependency resolution.
    if (!exists(NSID("vanilla")))
        create(NSID("vanilla"));

    _build_graph();
    _effective_cache.clear();
}

// ── Load datapack (pack.mcmeta detection + McOfficial parsing) ─────────

bool ProfileManager::load_datapack(const std::filesystem::path& dir) {
    // Validate this is actually a datapack.
    const auto mcmeta_path = dir / "pack.mcmeta";
    if (!std::filesystem::is_regular_file(mcmeta_path))
        return false;

    try {
        // The vanilla root is the implicit dependency (NOT declared via
        // `dependencies`); inject it so cross_validate has a base universe.
        if (!exists(NSID("vanilla")))
            create(NSID("vanilla"));

        const NSID name(derive_datapack_name(dir));

        // Phase 1: parse the datapack's own DTOs.
        auto [ench_data, eq_data] = McOfficialParser::parse(dir);

        // Phase 2: two-phase loading — build the vanilla universe into
        // TEMPORARY registries, then cross-validate the datapack's DTOs on top
        // of the union (mirrors ProfileLoader::load_into).  The datapack keeps
        // ONLY its own enchantments/equipments; the vanilla tag universe
        // (tag_reg) is retained so `#tag` supported_items resolve downstream.
        RegistryLoader loader;
        TagRegistry tag_reg;          // vanilla universe: tags
        EquipmentRegistry eq_reg;     // vanilla universe: equipment
        EnchantmentRegistry ench_reg; // vanilla universe + datapack content
        besq::data::load_builtin_data(tag_reg, ench_reg, eq_reg);
        loader.resolve_with_base(ench_data, eq_data, tag_reg, eq_reg, ench_reg);

        // Filter the union back to the datapack's own content.
        std::unordered_set<NSID> profile_ench_ids;
        for (const auto& d : ench_data)
            profile_ench_ids.insert(NSID(d.id));
        std::unordered_set<NSID> profile_eq_ids;
        for (const auto& d : eq_data)
            profile_eq_ids.insert(NSID(d.id));

        EnchantmentRegistry profile_ench;
        for (const auto& [id, info] : ench_reg.data())
            if (profile_ench_ids.count(id) != 0)
                profile_ench.insert(info);
        EquipmentRegistry profile_eq;
        for (const auto& [id, eq] : eq_reg.data())
            if (profile_eq_ids.count(id) != 0)
                profile_eq.insert(eq);

        // Construct the Profile and attach the builtin tag resolver.
        Profile profile(ProfileMetadata(name), std::move(profile_ench),
                        std::move(profile_eq), std::move(tag_reg));
        profile.set_tag_resolver(besq::data::make_builtin_tag_resolver());

        // Register (replace-on-conflict) and rebuild the dependency graph.
        if (exists(name))
            remove(name);
        _profiles[name] = std::make_unique<Profile>(std::move(profile));
        _build_graph();

        // Cross-validate the datapack's supported_items refs against
        // (vanilla ∪ target's own vanilla tag universe).
        cross_validate(name);

        _effective_cache.clear();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load datapack from '%s': %s", dir.string().c_str(), e.what());
        return false;
    }
}

// ── Cross-validate supported_items against the dependency universe ────

size_t ProfileManager::cross_validate(const NSID& profile) {
    Profile* target = _find(profile);
    if (!target)
        return 0;

    const auto deps = resolve_dependencies(profile);

    // Validation universe = vanilla base ∪ dependency chain (equipment + tags).
    std::unordered_set<NSID> universe_eq;
    std::unordered_set<NSID> universe_tags;
    auto collect = [&](const NSID& name) {
        const Profile* p = _find(name);
        if (!p)
            return;
        for (const auto& [id, eq] : p->eq().data())
            universe_eq.insert(id);
        for (const auto& [id, tag] : p->tags().data())
            universe_tags.insert(id);
    };
    collect(NSID("vanilla"));
    for (const auto& d : deps)
        collect(d);
    // A datapack keeps the vanilla tag universe in its OWN tags registry, so
    // its `#tag` supported_items references must also validate against its own
    // equipment/tags (self-contained datapack tags resolve here too).
    collect(profile);

    // Drop supported_items refs that fail validation; drop the whole
    // enchantment if no valid ref remains.
    size_t removed = 0;
    std::vector<NSID> to_remove;
    std::vector<EnchInfo> to_update;
    for (const auto& [id, info] : target->ench().data()) {
        if (info.supported_items.empty())
            continue;
        EnchInfo updated = info;
        bool changed = false;
        for (auto it = updated.supported_items.begin();
             it != updated.supported_items.end();) {
            if (it->is_tag() ? universe_tags.count(*it) : universe_eq.count(*it)) {
                ++it;
            } else {
                it = updated.supported_items.erase(it);
                ++removed;
                changed = true;
            }
        }
        if (updated.supported_items.empty()) {
            to_remove.push_back(id);
        } else if (changed) {
            to_update.push_back(std::move(updated));
        }
    }
    for (const auto& id : to_remove)
        target->remove_enchantment(id);
    for (const auto& info : to_update)
        target->update_enchantment(info);

    _effective_cache.clear();
    return removed;
}

// ── Publish (flatten effective view + version/tag) ──────────────────────

bool ProfileManager::publish(const NSID& profile, const std::string& version,
                             const std::string& tag, const std::filesystem::path& out) {
    if (_find(profile) == nullptr) return false;
    const Profile& eff = resolve_effective(profile);
    Json json = eff.to_json();
    json.set("version", Json(version));
    if (!tag.empty())
        json.set("release_tag", Json(tag));
    std::ofstream f(out);
    if (!f) return false;
    f << json.to_string(Json::Pretty);
    return true;
}
