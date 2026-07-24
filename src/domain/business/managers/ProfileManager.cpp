#include "ProfileManager.h"
#include "builtin/DataLoader.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <stdexcept>

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
    p->_meta.parent = source.str();
    p->_meta.version = "snapshot";  // mark as snapshot
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
    p->_meta.parent = source.str();
    Profile& ref = *p;
    _profiles[branch_name] = std::move(p);
    return ref;
}

// ── Merge ─────────────────────────────────────────────────────────────

void ProfileManager::merge(const NSID& source, const NSID& dest) {
    if (source == dest) return;  // self-merge is no-op
    const Profile& src = *find(source);
    Profile& dst = *find(dest);

    // Enchantments: overwrite existing, add new
    for (const auto& [nsid, ench] : src._ench.data()) {
        if (dst._ench.contains(nsid))
            dst._ench.insert_or_assign(ench);
        else
            dst._ench.insert(ench);
    }

    // Equipment: add if not already present
    for (const auto& [id, eq] : src._eq.data()) {
        if (!dst._eq.contains(id))
            dst._eq.insert(eq);
    }

    // Tags: ensure present
    for (const auto& [nsid, tag] : src._tags.data()) {
        if (!dst._tags.contains(nsid))
            dst._tags.insert(tag);
    }

    dst._meta.updated_at = std::chrono::system_clock::now();
}
