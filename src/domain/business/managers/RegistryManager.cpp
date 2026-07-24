#include "RegistryManager.h"

#include <algorithm>
#include <unordered_set>

// ============================================================================
// Builder
// ============================================================================

RegistryManager& RegistryManager::load(const Profile& from) {
    _ench = from._ench;
    _eq   = from._eq;
    _tags = from._tags;
    return *this;
}

RegistryManager& RegistryManager::filter(std::function<bool(const EnchInfo&)> pred) {
    if (_ench) {
        EnchantmentRegistry filtered;
        for (const auto& [id, info] : _ench->data()) {
            if (pred(info))
                filtered.insert(info);
        }
        _ench = std::move(filtered);
    }
    return *this;
}

RegistryManager& RegistryManager::filter_platform(MCE platform) {
    return filter([platform](const EnchInfo& info) {
        return info.supported_platform == MCE::All ||
               info.supported_platform == platform;
    });
}

RegistryManager& RegistryManager::filter_equipment(const NSID& category) {
    return filter([&category](const EnchInfo& info) {
        return info.applicable_equipments.find(category) !=
               info.applicable_equipments.end();
    });
}

RegistryManager& RegistryManager::unite(const Profile& other) {
    if (_ench && other._ench.size() > 0) {
        for (const auto& [id, info] : other._ench.data())
            _ench->insert_or_assign(info);
    } else if (other._ench.size() > 0) {
        _ench = other._ench;
    }

    if (_eq && other._eq.size() > 0) {
        for (const auto& [id, eq] : other._eq.data())
            _eq->insert_or_assign(eq);
    } else if (other._eq.size() > 0) {
        _eq = other._eq;
    }

    if (_tags && other._tags.size() > 0) {
        for (const auto& [id, tag] : other._tags.data())
            _tags->insert_or_assign(tag);
    } else if (other._tags.size() > 0) {
        _tags = other._tags;
    }

    return *this;
}

RegistryManager& RegistryManager::intersect(const Profile& other) {
    if (_ench) {
        EnchantmentRegistry result;
        for (const auto& [id, info] : _ench->data()) {
            if (other._ench.contains(id))
                result.insert(info);
        }
        _ench = std::move(result);
    }

    if (_eq) {
        EquipmentRegistry result;
        for (const auto& [id, eq] : _eq->data()) {
            if (other._eq.contains(id))
                result.insert(eq);
        }
        _eq = std::move(result);
    }

    if (_tags) {
        EquipmentTagRegistry result;
        for (const auto& [id, tag] : _tags->data()) {
            if (other._tags.contains(id))
                result.insert(tag);
        }
        _tags = std::move(result);
    }

    return *this;
}

Profile RegistryManager::build(const NSID& result_name) const {
    Profile p(result_name);
    if (_ench) p._ench = *_ench;
    if (_eq)   p._eq   = *_eq;
    if (_tags) p._tags = *_tags;
    return p;
}

// ============================================================================
// Static operations
// ============================================================================

Profile RegistryManager::unite(
    const NSID& name, const Profile& a, const Profile& b)
{
    RegistryManager builder;
    builder.load(a).unite(b);
    return builder.build(name);
}

Profile RegistryManager::intersect(
    const NSID& name, const Profile& a, const Profile& b)
{
    RegistryManager builder;
    builder.load(a).intersect(b);
    return builder.build(name);
}

Profile RegistryManager::subtract(
    const NSID& name, const Profile& base, const Profile& other)
{
    Profile p(base.clone(name));

    // Remove enchantments that exist in other
    EnchantmentRegistry ench_result;
    for (const auto& [id, info] : p._ench.data()) {
        if (!other._ench.contains(id))
            ench_result.insert(info);
    }
    p._ench = std::move(ench_result);

    // Remove equipment that exists in other
    EquipmentRegistry eq_result;
    for (const auto& [id, eq] : p._eq.data()) {
        if (!other._eq.contains(id))
            eq_result.insert(eq);
    }
    p._eq = std::move(eq_result);

    // Remove tags that exist in other
    EquipmentTagRegistry tag_result;
    for (const auto& [id, tag] : p._tags.data()) {
        if (!other._tags.contains(id))
            tag_result.insert(tag);
    }
    p._tags = std::move(tag_result);

    return p;
}

Profile RegistryManager::merge(
    const NSID& name, const Profile& base, const Profile& other)
{
    Profile p(base.clone(name));

    // Merge enchantments (other overwrites base)
    for (const auto& [id, info] : other._ench.data())
        p._ench.insert_or_assign(info);

    // Merge equipment
    for (const auto& [id, eq] : other._eq.data())
        p._eq.insert_or_assign(eq);

    // Merge tags
    for (const auto& [id, tag] : other._tags.data())
        p._tags.insert_or_assign(tag);

    return p;
}

// ============================================================================
// Diff
// ============================================================================

RegistryManager::DiffResult RegistryManager::diff(
    const Profile& a, const Profile& b)
{
    DiffResult result;

    auto diff_registries = [](const auto& reg_a, const auto& reg_b) {
        std::vector<DiffEntry> entries;

        // Find additions and modifications
        for (const auto& [id, entry] : reg_b.data()) {
            auto it = reg_a.find(id);
            if (it == reg_a.end()) {
                entries.push_back({id, DiffEntry::Added});
            } else if (*it != entry) {
                entries.push_back({id, DiffEntry::Modified});
            }
        }

        // Find removals
        for (const auto& [id, entry] : reg_a.data()) {
            if (reg_b.find(id) == reg_b.end())
                entries.push_back({id, DiffEntry::Removed});
        }

        return entries;
    };

    result.enchantments = diff_registries(a._ench, b._ench);
    result.equipment    = diff_registries(a._eq, b._eq);
    result.tags         = diff_registries(a._tags, b._tags);

    return result;
}

// ============================================================================
// Validation
// ============================================================================

bool RegistryManager::validate(const Profile& profile) {
    return profile.validate();
}

// ============================================================================
// Operator overloads
// ============================================================================

Profile operator|(const Profile& a, const Profile& b) {
    return RegistryManager::unite(NSID(), a, b);
}

Profile operator&(const Profile& a, const Profile& b) {
    return RegistryManager::intersect(NSID(), a, b);
}

Profile operator+(const Profile& a, const Profile& b) {
    return RegistryManager::merge(NSID(), a, b);
}

Profile operator-(const Profile& a, const Profile& b) {
    return RegistryManager::subtract(NSID(), a, b);
}
