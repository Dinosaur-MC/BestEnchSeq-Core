#include "RegistryHelper.h"

#include <unordered_set>

// ============================================================================
// Builder
// ============================================================================

RegistryHelper& RegistryHelper::load(const Profile& from) {
    // Deep-copy registries via Json serialization (no friend access needed)
    auto copy_reg = [](const auto& src) -> std::decay_t<decltype(src)> {
        std::decay_t<decltype(src)> dst;
        for (const auto& [id, entry] : src.data())
            dst.insert(entry);
        return dst;
    };
    _ench = copy_reg(from.ench());
    _eq   = copy_reg(from.eq());
    _tags = copy_reg(from.tags());
    return *this;
}

RegistryHelper& RegistryHelper::filter(std::function<bool(const EnchInfo&)> pred) {
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

RegistryHelper& RegistryHelper::filter_platform(MCE platform) {
    return filter([platform](const EnchInfo& info) {
        return info.supported_platform == MCE::All ||
               info.supported_platform == platform;
    });
}

RegistryHelper& RegistryHelper::filter_equipment(const NSID& category) {
    return filter([&category](const EnchInfo& info) {
        return info.supported_items.find(category) !=
               info.supported_items.end();
    });
}

RegistryHelper& RegistryHelper::unite(const Profile& other) {
    auto unite_reg = [](auto& opt, const auto& src) {
        if (opt && src.size() > 0) {
            for (const auto& [id, entry] : src.data())
                opt->insert_or_assign(entry);
        } else if (src.size() > 0) {
            std::decay_t<decltype(src)> reg;
            for (const auto& [id, entry] : src.data())
                reg.insert(entry);
            opt = std::move(reg);
        }
    };
    unite_reg(_ench, other.ench());
    unite_reg(_eq, other.eq());
    unite_reg(_tags, other.tags());
    return *this;
}

RegistryHelper& RegistryHelper::intersect(const Profile& other) {
    if (_ench) {
        EnchantmentRegistry result;
        for (const auto& [id, info] : _ench->data()) {
            if (other.ench().contains(id))
                result.insert(info);
        }
        _ench = std::move(result);
    }

    if (_eq) {
        EquipmentRegistry result;
        for (const auto& [id, eq] : _eq->data()) {
            if (other.eq().contains(id))
                result.insert(eq);
        }
        _eq = std::move(result);
    }

    if (_tags) {
        TagRegistry result;
        for (const auto& [id, tag] : _tags->data()) {
            if (other.tags().contains(id))
                result.insert(tag);
        }
        _tags = std::move(result);
    }

    return *this;
}

Profile RegistryHelper::build(const NSID& result_name) const {
    return Profile(
        ProfileMetadata(result_name),
        _ench.value_or(EnchantmentRegistry{}),
        _eq.value_or(EquipmentRegistry{}),
        _tags.value_or(TagRegistry{})
    );
}

// ============================================================================
// Static operations
// ============================================================================

Profile RegistryHelper::unite(
    const NSID& name, const Profile& a, const Profile& b)
{
    RegistryHelper builder;
    builder.load(a).unite(b);
    return builder.build(name);
}

Profile RegistryHelper::intersect(
    const NSID& name, const Profile& a, const Profile& b)
{
    RegistryHelper builder;
    builder.load(a).intersect(b);
    return builder.build(name);
}

Profile RegistryHelper::subtract(
    const NSID& name, const Profile& base, const Profile& other)
{
    // Enchantments: keep those NOT in other
    EnchantmentRegistry ench_result;
    for (const auto& [id, info] : base.ench().data()) {
        if (!other.ench().contains(id))
            ench_result.insert(info);
    }

    // Equipment: keep those NOT in other
    EquipmentRegistry eq_result;
    for (const auto& [id, eq] : base.eq().data()) {
        if (!other.eq().contains(id))
            eq_result.insert(eq);
    }

    // Tags: keep those NOT in other
    TagRegistry tag_result;
    for (const auto& [id, tag] : base.tags().data()) {
        if (!other.tags().contains(id))
            tag_result.insert(tag);
    }

    return Profile(ProfileMetadata(name), std::move(ench_result),
                   std::move(eq_result), std::move(tag_result));
}

Profile RegistryHelper::merge(
    const NSID& name, const Profile& base, const Profile& other)
{
    Profile p = base.clone(name);

    // Merge enchantments (other overwrites base)
    for (const auto& [id, info] : other.ench().data())
        p.add_enchantment(info);

    // Merge equipment
    for (const auto& [id, eq] : other.eq().data())
        p.add_equipment(eq);

    // Merge tags
    for (const auto& [id, tag] : other.tags().data())
        p.add_tag(tag);

    return p;
}

// ============================================================================
// Diff
// ============================================================================

RegistryHelper::DiffResult RegistryHelper::diff(
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

    result.enchantments = diff_registries(a.ench(), b.ench());
    result.equipment    = diff_registries(a.eq(), b.eq());
    result.tags         = diff_registries(a.tags(), b.tags());

    return result;
}

// ============================================================================
// Validation
// ============================================================================

bool RegistryHelper::validate(const Profile& profile) {
    return profile.validate();
}

// ============================================================================
// Operator overloads
// ============================================================================

Profile operator|(const Profile& a, const Profile& b) {
    return RegistryHelper::unite(NSID(), a, b);
}

Profile operator&(const Profile& a, const Profile& b) {
    return RegistryHelper::intersect(NSID(), a, b);
}

Profile operator+(const Profile& a, const Profile& b) {
    return RegistryHelper::merge(NSID(), a, b);
}

Profile operator-(const Profile& a, const Profile& b) {
    return RegistryHelper::subtract(NSID(), a, b);
}
