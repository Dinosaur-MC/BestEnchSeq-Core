#pragma once
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include <string>
#include <vector>

/// A named snapshot of all three domain registries.
struct Profile {
    std::string name;
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;
    EquipmentCategoryRegistry cat_reg;
    bool builtin_loaded = false;
};

/// Manages a collection of Profile instances.
class ProfileSet {
public:
    ProfileSet() = default;

    /// Create and load the "default" profile with built-in vanilla data.
    /// Idempotent — subsequent calls are no-ops.
    void load_builtin();

    /// Profile CRUD
    void create(const std::string& name);
    void fork(const std::string& source, const std::string& dest);
    void merge(const std::string& source, const std::string& dest);
    void remove(const std::string& name);
    bool exists(const std::string& name) const;
    std::vector<std::string> list() const;

    /// Activation
    void activate(const std::string& name);
    const std::string& active_name() const;
    Profile& active();
    const Profile& active() const;

    /// Access by name
    Profile& get(const std::string& name);
    const Profile& get(const std::string& name) const;

private:
    std::vector<Profile> _profiles;
    size_t _active = 0;

    Profile* _find(const std::string& name);
    const Profile* _find(const std::string& name) const;
};
