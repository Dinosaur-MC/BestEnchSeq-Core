#pragma once
#include "common.h"

#include <cstdint>
#include <string>
#include <unordered_set>

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
        size_t operator()(const Ench& e) const { return std::hash<int32_t>()(e.id); }
    };

    bool operator==(const Ench& other) const;
    int32_t operator+(int32_t lvl) const;
    int32_t operator+=(int32_t lvl);
    Ench operator+(const Ench& other) const;
    Ench& operator+=(const Ench& other);

    // Metadata queries — delegate to EnchantmentRegistry::get_instance()
    std::string get_name() const;
    platform::MCE get_supported_platform() const;
    int32_t get_max_level() const;
    int32_t get_limited_level() const;
    int32_t get_multiplier(bool is_book = false) const;
    const std::unordered_set<int32_t>& get_exclusive_set() const;
    const std::unordered_set<int32_t>& get_applicable_equipment() const;

    bool is_incompatible(const Ench& other) const;
};
