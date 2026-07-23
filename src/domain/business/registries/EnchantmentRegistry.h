#pragma once
#include "domain/business/types/Enchantment.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class EnchantmentRegistry {
  public:
    EnchantmentRegistry() = default;
    EnchantmentRegistry(const EnchantmentRegistry &) = default;            // copy constructor
    EnchantmentRegistry(EnchantmentRegistry &&) = default;
    EnchantmentRegistry &operator=(const EnchantmentRegistry &) = default; // copy assignment
    EnchantmentRegistry &operator=(EnchantmentRegistry &&) = default;      // no reassignment after init

    // Lifecycle
    void initialize(const std::vector<EnchInfo> &infos);
    /// Reset all state — for testing only. Invalidates all held references.
    void reset_for_testing();

    // Lookup (hot-path O(1))
    const EnchInfo &get(int32_t index) const;
    const EnchInfo &get(const std::string &name_id) const;
    int32_t get_id(const std::string &name_id) const;
    const std::vector<EnchInfo> &get_instances() const { return instances_; }
    size_t size() const { return instances_.size(); }

    // Incompatibility table
    const std::unordered_set<int32_t> &get_exclusive_set(int32_t e) const;
    bool is_incompatible(int32_t e1, int32_t e2) const;

    /// Add a single EnchInfo. Returns false if name_id already exists.
    bool add(const EnchInfo& info);

    /// Remove by name_id. Returns false if not found.
    bool remove(const std::string& name_id);

    /// Update specific fields of existing entry identified by name_id.
    /// Supported: max_level, limited_level, multiplier, name, exclusive_set (replaces).
    bool modify(const std::string& name_id, const EnchInfo& patch);

    // Validation (moved from EnchInfo statics)
    static bool check_validation(const std::vector<EnchInfo> &infos);

  private:
    std::vector<EnchInfo> instances_;
    std::unordered_map<std::string, int32_t> name_to_index_;
    std::unordered_map<int32_t, std::unordered_set<int32_t>> incompatible_table_;
};
