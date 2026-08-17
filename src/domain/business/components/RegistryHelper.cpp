#include "RegistryHelper.h"
#include "domain/business/components/TagResolver.h"

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

Profile RegistryHelper::build(const std::string& result_name) const {
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
    const std::string& name, const Profile& a, const Profile& b)
{
    RegistryHelper builder;
    builder.load(a).unite(b);
    return builder.build(name);
}

Profile RegistryHelper::intersect(
    const std::string& name, const Profile& a, const Profile& b)
{
    RegistryHelper builder;
    builder.load(a).intersect(b);
    return builder.build(name);
}

Profile RegistryHelper::subtract(
    const std::string& name, const Profile& base, const Profile& other)
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

void RegistryHelper::merge(Profile& dest, const Profile& src) {
    // Enchantments: src wins on conflict (insert_or_assign semantics).
    // `update_enchantment` overwrites an existing entry; `add_enchantment`
    // inserts a new one.
    for (const auto& [id, info] : src.ench().data()) {
        if (dest.ench().contains(id))
            dest.update_enchantment(info);
        else
            dest.add_enchantment(info);
    }

    // Equipment: src wins on conflict (insert_or_assign) — same direction as
    // enchantments and the documented merge semantics (ProfileManager.h:
    // "Source entries overwrite dest entries on conflict").  The old
    // add-if-absent let the LOWER layer win in effective views, silently
    // dropping a datapack's field override of a vanilla equipment (e.g.
    // max_durability=999 on copper_boots resolved to vanilla's 143).  Junk
    // protection is unnecessary here: entries entering a merge already went
    // through resolve_own_content's field-level merge + validation, so a
    // partial entry cannot clobber a complete one (empty/0 fields kept old).
    for (const auto& [id, eq] : src.eq().data()) {
        if (dest.eq().contains(id))
            dest.remove_equipment(id);  // Profile has no update_equipment — remove+add
        dest.add_equipment(eq);
    }

    // Tags: src wins on conflict (insert_or_assign) — same direction as
    // enchantments/equipment; the member data lives in the TagResolver
    // (LAST source wins, see build_tag_resolver), the registry name follows
    // the same upper-over-lower rule.
    for (const auto& [id, tag] : src.tags().data()) {
        if (dest.tags().contains(id))
            dest.remove_tag(id);
        dest.add_tag(tag);
    }
}

Profile RegistryHelper::merge(
    const std::string& name, const Profile& base, const Profile& other)
{
    Profile p = base.clone(name);
    merge(p, other);  // other wins on conflict
    return p;
}

std::shared_ptr<TagResolver> RegistryHelper::build_tag_resolver(
    const Profile& eff, const std::vector<const Profile*>& sources)
{
    auto resolver = std::make_shared<TagResolver>();
    for (const auto& [tag_nsid, tag] : eff.tags().data()) {
        const std::string key = tag_nsid.str();
        if (key.empty() || key[0] != '#')
            continue;  // only `#tag` refs live in the resolver

        const std::string tag_key = key.substr(1);

        // Member data: overwrite as we iterate so the LAST (highest-priority)
        // source whose attached resolver defines the tag wins — matching the
        // effective-view merge direction (upper overrides lower, B-T26 #19).
        // Sources without a resolver (e.g. manually-built test profiles) yield
        // an empty member set — the tag key stays registered and queryable.
        std::unordered_set<std::string> members;
        for (const Profile* src : sources) {
            if (!src)
                continue;
            const TagResolver* tr = src->tag_resolver();
            if (!tr)
                continue;
            const auto pos = tag_key.find(':');
            if (pos == std::string::npos)
                continue;  // unnamespaced key: no ns/name member lookup
            const std::string ns   = tag_key.substr(0, pos);
            const std::string name = tag_key.substr(pos + 1);
            if (const auto* m = tr->get_tag(ns, name))
                members = *m;  // later (higher-priority) sources override earlier
        }
        resolver->add_tag(tag_key, std::move(members));
    }
    return resolver;
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
    // Cross-check exclusive_set references (mirrors Profile::validate, which
    // intentionally does NOT hard-fail on the display-only `eq.category`).
    if (!profile.validate())
        return false;

    // Every enchantment must have a sane max level.
    for (const auto& [id, info] : profile.ench().data()) {
        if (info.max_level < 1)
            return false;
    }
    return true;
}

// ============================================================================
// Operator overloads
// ============================================================================

Profile operator|(const Profile& a, const Profile& b) {
    return RegistryHelper::unite("", a, b);
}

Profile operator&(const Profile& a, const Profile& b) {
    return RegistryHelper::intersect("", a, b);
}

Profile operator+(const Profile& a, const Profile& b) {
    return RegistryHelper::merge("", a, b);
}

Profile operator-(const Profile& a, const Profile& b) {
    return RegistryHelper::subtract("", a, b);
}
