#pragma once
#include "domain/business/types/EnchInfo.h"

#include <cstdint>
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
    const EnchInfo &get(const NSID &id) const;
    int32_t get_id(const NSID &id) const;
    const std::vector<EnchInfo> &get_instances() const { return instances_; }
    size_t size() const { return instances_.size(); }

    // Incompatibility table
    const std::unordered_set<NSID> &get_exclusive_set(const NSID &e) const;
    bool is_incompatible(const NSID &e1, const NSID &e2) const;

    // Mutable operations (for incremental editing)
    bool add(const EnchInfo &info);
    bool remove(const NSID &id);
    bool modify(const NSID &id, const EnchInfo &patch);
    // String overloads for backward compat (convert to NSID internally)
    bool remove(const std::string &name_id);
    bool modify(const std::string &name_id, const EnchInfo &patch);

    // Validation
    static bool check_validation(const std::vector<EnchInfo> &infos);

  private:
    std::vector<EnchInfo> instances_;
    std::unordered_map<NSID, int32_t> name_to_index_;
    std::unordered_map<NSID, std::unordered_set<NSID>> incompatible_table_;
};
