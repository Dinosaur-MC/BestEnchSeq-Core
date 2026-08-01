#include "ProfileManager.h"
#include "domain/business/components/RegistryHelper.h"
#include "domain/business/loaders/ProfileLoader.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <unordered_set>

// ── Internal helpers ──────────────────────────────────────────────────

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
    return ref;
}

bool ProfileManager::remove(const NSID& name) {
    auto it = _profiles.find(name);
    if (it == _profiles.end()) return false;

    _profiles.erase(it);

    // Adjust active if needed
    if (_profiles.empty()) {
        _active = NSID();
    } else if (_active == name) {
        _active = _profiles.begin()->first;
    }
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
    return ref;
}

// ── Merge ─────────────────────────────────────────────────────────────

void ProfileManager::merge(const NSID& source, const NSID& dest) {
    if (source == dest) return;  // self-merge is no-op
    const Profile& src = *find(source);
    Profile& dst = *find(dest);
    // Source wins on conflict, merged in place (dest metadata preserved).
    RegistryHelper::merge(dst, src);
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

// ── Load directory ────────────────────────────────────────────────────

void ProfileManager::load_directory(const std::filesystem::path& dir) {
    if (!std::filesystem::is_directory(dir))
        return;

    ProfileLoader loader;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        const auto& path = entry.path();
        if (path.extension() != ".json")
            continue;

        Profile loaded = loader.load(path);
        const NSID name = loaded.name().empty() ? NSID(path.stem().string()) : loaded.name();
        if (exists(name))
            remove(name);  // replace-on-conflict
        _profiles[name] = std::make_unique<Profile>(std::move(loaded));
    }

    // The vanilla base profile must exist for dependency resolution.
    if (!exists(NSID("vanilla")))
        create(NSID("vanilla"));

    _build_graph();
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

    return removed;
}
