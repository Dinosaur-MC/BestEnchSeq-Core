#pragma once

#include <cstdint>

class EnchantmentRegistry;  // forward decl for checked() factory

struct Ench {
    int32_t id = 0;     // now defaulted — allows fast default construction
    int32_t level = 1;

    // Tag type for unchecked (hot-path) construction — no validation
    struct Unchecked {};
    static constexpr Unchecked unchecked{};

    // Hot-path unchecked constructor — no validation, no overhead
    Ench(int32_t id, int32_t level, Unchecked) : id(id), level(level) {}

    // Checked constructors (validate against global registry singleton)
    Ench(int32_t id);
    Ench(int32_t id, int32_t level);

    // Explicit-registry factory (for subset / testing)
    static Ench checked(int32_t id, int32_t level, const EnchantmentRegistry& reg);

    struct Hash {
        size_t operator()(const Ench& e) const {
            return static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16);
        }
    };

    bool operator==(const Ench& other) const { return id == other.id && level == other.level; }

    // NOTE: Registry-dependent metadata queries (get_name, get_max_level, etc.)
    // have been removed from the domain type. Use EnchantmentRegistry directly:
    //   reg.get(ench.id).name_id         // instead of ench.get_name()
    //   reg.get(ench.id).max_level       // instead of ench.get_max_level()
    //   reg.get_exclusive_set(ench.id)   // instead of ench.get_exclusive_set()
    //   reg.is_incompatible(e1.id, e2.id)// instead of e1.is_incompatible(e2)
};
