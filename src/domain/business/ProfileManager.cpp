#include "ProfileManager.h"
#include "domain/business/ProfileNaming.h"
#include "domain/business/components/RegistryHelper.h"
#include "domain/business/components/Serializer.h"  // Profile << Json (snapshot)
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/parsers/McOfficialParser.h"
#include "builtin/DataLoader.h"
#include "common/io/FileUtils.hpp"
#include "common/io/json.h"
#include "common/log/log.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_set>

// ── Datapack → profile naming (declared in ProfileNaming.h) ───────────

std::string derive_datapack_name(const std::filesystem::path& dir) {
    // Profile keys are plain std::string (B-T13), so the datapack name is
    // taken VERBATIM from pack.id (else the directory stem) with no charset
    // cleanup and no leading-digit guard — spaces/dots are preserved as-is.
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
    if (raw.empty())
        raw = "datapack";
    // A datapack must never replace the injected vanilla base profile.
    if (raw == "vanilla")
        raw = "vanilla_datapack";
    return raw;
}

Profile* ProfileManager::_find(const std::string& name) {
    auto it = _profiles.find(name);
    return (it != _profiles.end()) ? it->second.get() : nullptr;
}

const Profile* ProfileManager::_find(const std::string& name) const {
    auto it = _profiles.find(name);
    return (it != _profiles.end()) ? it->second.get() : nullptr;
}

// ── CRUD ──────────────────────────────────────────────────────────────

Profile& ProfileManager::create(const std::string& name) {
    if (exists(name))
        throw std::runtime_error("Profile already exists: " + name);
    auto p = std::make_unique<Profile>(name);
    Profile& ref = *p;
    _profiles[name] = std::move(p);
    _effective_cache.clear();
    return ref;
}

Profile& ProfileManager::create_from(const std::string& source, const std::string& dest) {
    if (!exists(source))
        throw std::runtime_error("Source profile not found: " + source);
    if (exists(dest))
        throw std::runtime_error("Destination profile already exists: " + dest);

    const Profile& src = *find(source);
    auto p = std::make_unique<Profile>(src.clone(dest));
    Profile& ref = *p;
    _profiles[dest] = std::move(p);
    _effective_cache.clear();
    return ref;
}

bool ProfileManager::remove(const std::string& name) {
    auto it = _profiles.find(name);
    if (it == _profiles.end()) return false;

    _profiles.erase(it);
    _undo_log.erase(name);  // 清理该 profile 的 undo 历史

    // Adjust active if needed
    if (_profiles.empty()) {
        _active.clear();
    } else if (_active == name) {
        _active = _profiles.begin()->first;
    }
    _effective_cache.clear();
    return true;
}

bool ProfileManager::exists(const std::string& name) const {
    return _profiles.find(name) != _profiles.end();
}

Profile* ProfileManager::find(const std::string& name) {
    return _find(name);
}

const Profile* ProfileManager::find(const std::string& name) const {
    return _find(name);
}

std::vector<std::string> ProfileManager::list() const {
    std::vector<std::string> names;
    names.reserve(_profiles.size());
    for (const auto& [name, _] : _profiles)
        names.push_back(name);
    return names;
}

// ── Stable CRUD (real-time validation + snapshot/undo) ────────────────

bool ProfileManager::_mutate(const std::string& profile, std::function<bool(Profile&)> op) {
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

bool ProfileManager::add_enchantment(const std::string& profile, const EnchInfo& info) {
    return _mutate(profile, [&](Profile& p) { return p.add_enchantment(info); });
}

bool ProfileManager::update_enchantment(const std::string& profile, const EnchInfo& patch) {
    return _mutate(profile, [&](Profile& p) { return p.update_enchantment(patch); });
}

bool ProfileManager::remove_enchantment(const std::string& profile, const NSID& id) {
    return _mutate(profile, [&](Profile& p) { return p.remove_enchantment(id); });
}

bool ProfileManager::add_equipment(const std::string& profile, const Equipment& eq) {
    return _mutate(profile, [&](Profile& p) { return p.add_equipment(eq); });
}

bool ProfileManager::remove_equipment(const std::string& profile, const NSID& id) {
    return _mutate(profile, [&](Profile& p) { return p.remove_equipment(id); });
}

bool ProfileManager::add_tag(const std::string& profile, const EquipmentTag& tag) {
    return _mutate(profile, [&](Profile& p) { return p.add_tag(tag); });
}

bool ProfileManager::remove_tag(const std::string& profile, const NSID& id) {
    return _mutate(profile, [&](Profile& p) { return p.remove_tag(id); });
}

bool ProfileManager::undo(const std::string& profile) {
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

void ProfileManager::activate(const std::string& name) {
    if (!exists(name))
        throw std::runtime_error("Profile not found: " + name);
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

Profile& ProfileManager::snapshot(const std::string& source, const std::string& snapshot_name) {
    if (!exists(source))
        throw std::runtime_error("Source profile not found: " + source);
    if (exists(snapshot_name))
        throw std::runtime_error("Snapshot name already exists: " + snapshot_name);

    const Profile& src = *find(source);
    auto p = std::make_unique<Profile>(src.clone(snapshot_name));
    p->set_version("snapshot");  // mark as snapshot
    Profile& ref = *p;
    _profiles[snapshot_name] = std::move(p);
    _effective_cache.clear();
    return ref;
}

// ── Branch ────────────────────────────────────────────────────────────

Profile& ProfileManager::branch(const std::string& source, const std::string& branch_name) {
    if (!exists(source))
        throw std::runtime_error("Source profile not found: " + source);
    if (exists(branch_name))
        throw std::runtime_error("Branch name already exists: " + branch_name);

    const Profile& src = *find(source);
    auto p = std::make_unique<Profile>(src.clone(branch_name));
    Profile& ref = *p;
    _profiles[branch_name] = std::move(p);
    _effective_cache.clear();
    return ref;
}

// ── Merge ─────────────────────────────────────────────────────────────

void ProfileManager::merge(const std::string& source, const std::string& dest) {
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
    for (const auto& [name, p] : _profiles)
        _dep_graph[name] = p->dependencies();
}

std::vector<std::string> ProfileManager::resolve_dependencies(const std::string& profile) const {
    // Rebuild from the current profiles so direct set_dependencies() calls
    // (which bypass the manager) are always honored.
    _build_graph();

    std::vector<std::string> order;
    std::unordered_map<std::string, uint8_t> color;   // 0 白 1 灰 2 黑
    std::function<bool(const std::string&)> dfs = [&](const std::string& n) -> bool {
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

const Profile& ProfileManager::resolve_effective(const std::string& profile) const {
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
            const std::string name = loaded.name().empty() ? path.stem().string() : loaded.name();
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
    if (!exists("builtin:vanilla"))
        create("builtin:vanilla");

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
        // All fallible work (parse, two-phase resolve, NSID/profile
        // construction) happens strictly ABOVE the manager commit point below,
        // so a failure never leaves a stray vanilla or half-registered profile.

        // Phase 1: parse the datapack's own DTOs.
        auto [ench_data, eq_data] = McOfficialParser::parse(dir);

        // Phase 2: two-phase loading — build the vanilla universe into
        // temporary registries, cross-validate the datapack's DTOs on top, then
        // filter back to the datapack's own content.  The vanilla tag universe
        // is retained so `#tag` supported_items references resolve downstream.
        auto own = RegistryLoader::resolve_own_content(ench_data, eq_data);

        const std::string name = derive_datapack_name(dir);

        // Construct the Profile and attach the builtin tag resolver.
        Profile profile(ProfileMetadata(name), std::move(own.ench),
                        std::move(own.eq), std::move(own.tags));
        profile.set_tag_resolver(besq::data::make_builtin_tag_resolver());

        // ── COMMIT POINT ──  From here the manager is mutated; nothing below
        // can fail in a way that leaves a half-registered profile behind.
        // The vanilla root is the implicit dependency (NOT declared via
        // `dependencies`); inject it so cross_validate has a base universe.
        if (!exists("builtin:vanilla"))
            create("builtin:vanilla");

        if (exists(name))
            remove(name);  // replace-on-conflict
        _profiles[name] = std::make_unique<Profile>(std::move(profile));

        _build_graph();
        cross_validate(name);  // clears _effective_cache
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load datapack from '%s': %s", dir.string().c_str(), e.what());
        return false;
    }
}

// ── Cross-validate supported_items against the dependency universe ────

size_t ProfileManager::cross_validate(const std::string& profile) {
    Profile* target = _find(profile);
    if (!target)
        return 0;

    const auto deps = resolve_dependencies(profile);

    // Validation universe = vanilla base ∪ dependency chain (equipment + tags).
    std::unordered_set<NSID> universe_eq;
    std::unordered_set<NSID> universe_tags;
    auto collect = [&](const std::string& name) {
        const Profile* p = _find(name);
        if (!p)
            return;
        for (const auto& [id, eq] : p->eq().data())
            universe_eq.insert(id);
        for (const auto& [id, tag] : p->tags().data())
            universe_tags.insert(id);
    };
    collect("builtin:vanilla");
    for (const auto& d : deps)
        collect(d);
    // The target's own profile re-contributes its own equipment (so concrete
    // item refs it defines resolve) and re-adds the vanilla tag universe that a
    // loaded profile retains in its own tags registry.  NOTE: this does NOT
    // make datapack-defined custom `#tags` resolve — McOfficialParser does not
    // retain datapack tag definitions in the profile's tags registry.
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

bool ProfileManager::publish(const std::string& profile, const std::string& version,
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
