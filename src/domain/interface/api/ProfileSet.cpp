#include "domain/interface/api/ProfileSet.h"
#include "builtin/DataLoader.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

// ── Builtin loader ────────────────────────────────────────────────────────────

void ProfileSet::load_builtin() {
    if (!exists("default")) {
        Profile p;
        p.name = "default";
        _profiles.push_back(std::move(p));
        _active = 0;
    }

    Profile& p = get("default");
    if (p.builtin_loaded) {
        return; // idempotent
    }

    // Delegate to the existing data loader, which handles filesystem-first
    // resolution with embedded-data fallback.
    besq::data::load_builtin_data(p.cat_reg, p.ench_reg, p.eq_reg);
    p.builtin_loaded = true;
}

// ── CRUD ──────────────────────────────────────────────────────────────────────

void ProfileSet::create(const std::string& name) {
    if (exists(name)) {
        throw std::runtime_error("Profile already exists: " + name);
    }
    Profile p;
    p.name = name;
    _profiles.push_back(std::move(p));
}

void ProfileSet::fork(const std::string& source, const std::string& dest) {
    if (!exists(source)) {
        throw std::runtime_error("Source profile not found: " + source);
    }
    if (exists(dest)) {
        throw std::runtime_error("Destination profile already exists: " + dest);
    }

    const Profile& src = get(source);
    Profile p;
    p.name = dest;
    p.ench_reg = src.ench_reg; // value-type deep copy
    p.eq_reg   = src.eq_reg;
    p.cat_reg  = src.cat_reg;
    p.builtin_loaded = src.builtin_loaded;
    _profiles.push_back(std::move(p));
}

void ProfileSet::merge(const std::string& source, const std::string& dest) {
    if (source == dest) return;  // self-merge is a no-op
    const Profile& src = get(source);
    Profile& dst = get(dest);

    // Enchantments: overwrite existing, add new
    for (const auto& ench : src.ench_reg.get_instances()) {
        if (ench.name_id.empty()) continue;
        if (dst.ench_reg.get_id(ench.name_id) >= 0) {
            dst.ench_reg.modify(ench.name_id, ench);
        } else {
            dst.ench_reg.add(ench);
        }
    }

    // Equipment: add if not already present
    for (const auto& eq : src.eq_reg.get_instances()) {
        if (eq.name_id.empty()) continue;
        if (dst.eq_reg.get_id(eq.name_id) < 0) {
            dst.eq_reg.add(eq);
        }
    }

    // Categories: idempotent add
    for (size_t i = 0; i < src.cat_reg.size(); ++i) {
        const auto& cat = src.cat_reg.get(static_cast<int32_t>(i));
        if (cat.name_id.empty()) continue;
        dst.cat_reg.add(cat.name_id);
    }
}

void ProfileSet::remove(const std::string& name) {
    auto it = std::find_if(_profiles.begin(), _profiles.end(),
        [&](const Profile& p) { return p.name == name; });
    if (it == _profiles.end()) {
        throw std::runtime_error("Profile not found: " + name);
    }

    size_t idx = std::distance(_profiles.begin(), it);
    _profiles.erase(it);

    // Adjust active index so it stays valid
    if (_profiles.empty()) {
        _active = 0;
    } else if (idx < _active) {
        --_active;
    } else if (idx == _active) {
        _active = 0;
    }
    // idx > _active → _active unchanged
}

bool ProfileSet::exists(const std::string& name) const {
    return _find(name) != nullptr;
}

std::vector<std::string> ProfileSet::list() const {
    std::vector<std::string> names;
    names.reserve(_profiles.size());
    for (const auto& p : _profiles) {
        names.push_back(p.name);
    }
    return names;
}

// ── Activation ────────────────────────────────────────────────────────────────

void ProfileSet::activate(const std::string& name) {
    auto it = std::find_if(_profiles.begin(), _profiles.end(),
        [&](const Profile& p) { return p.name == name; });
    if (it == _profiles.end()) {
        throw std::runtime_error("Profile not found: " + name);
    }
    _active = static_cast<size_t>(std::distance(_profiles.begin(), it));
}

const std::string& ProfileSet::active_name() const {
    if (_profiles.empty())
        throw std::runtime_error("No active profile — ProfileSet is empty");
    return _profiles[_active].name;
}

Profile& ProfileSet::active() {
    if (_profiles.empty())
        throw std::runtime_error("No active profile — ProfileSet is empty");
    return _profiles[_active];
}

const Profile& ProfileSet::active() const {
    if (_profiles.empty())
        throw std::runtime_error("No active profile — ProfileSet is empty");
    return _profiles[_active];
}

// ── Access by name ────────────────────────────────────────────────────────────

Profile& ProfileSet::get(const std::string& name) {
    auto* p = _find(name);
    if (!p) {
        throw std::runtime_error("Profile not found: " + name);
    }
    return *p;
}

const Profile& ProfileSet::get(const std::string& name) const {
    const auto* p = _find(name);
    if (!p) {
        throw std::runtime_error("Profile not found: " + name);
    }
    return *p;
}

// ── Internal helpers ──────────────────────────────────────────────────────────

Profile* ProfileSet::_find(const std::string& name) {
    for (auto& p : _profiles) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const Profile* ProfileSet::_find(const std::string& name) const {
    for (const auto& p : _profiles) {
        if (p.name == name) return &p;
    }
    return nullptr;
}
