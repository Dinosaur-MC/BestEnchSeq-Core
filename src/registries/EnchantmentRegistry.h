#pragma once
#include "types/EnchInfo.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ─── Forward declarations for serialization friends ───────────────────────
class ByteStreamWriter;
class ByteStreamReader;
class EnchantmentRegistry;

namespace compact_serial {
    void write(ByteStreamWriter& w, const EnchantmentRegistry& reg);
    EnchantmentRegistry read_enchantment_registry(ByteStreamReader& r);
}

class EnchantmentRegistry {
  public:
    EnchantmentRegistry() = default;
    EnchantmentRegistry(const EnchantmentRegistry &) = default;            // copy constructor
    EnchantmentRegistry(EnchantmentRegistry &&) = default;                 // create_subset() returns by value
    EnchantmentRegistry &operator=(const EnchantmentRegistry &) = default; // copy assignment
    EnchantmentRegistry &operator=(EnchantmentRegistry &&) = default;      // no reassignment after init

    // Lifecycle
    void initialize(const std::vector<EnchInfo> &infos);
    /// Reset all state — for testing only. Invalidates all held references.
    void reset_for_testing();

    // Subset derivation — pick by global index, create dense remapped copy
    EnchantmentRegistry create_subset(const std::vector<int32_t> &global_ids) const;

    // Bidirectional index mapping (identity on root registry)
    int32_t to_global_id(int32_t local_id) const;
    int32_t to_local_id(int32_t global_id) const;
    bool is_subset() const { return !local_to_global_.empty(); }

    // Lookup (hot-path O(1))
    const EnchInfo &get(int32_t index) const;
    const EnchInfo &get(const std::string &name_id) const;
    int32_t get_id(const std::string &name_id) const;
    const std::vector<EnchInfo> &get_instances() const { return instances_; }
    size_t size() const { return instances_.size(); }

    // Incompatibility table
    const std::unordered_set<int32_t> &get_exclusive_set(int32_t e) const;
    bool is_incompatible(int32_t e1, int32_t e2) const;

    // Validation (moved from EnchInfo statics)
    static bool check_validation(const std::vector<EnchInfo> &infos);

  private:
    friend void compact_serial::write(ByteStreamWriter& w, const EnchantmentRegistry& reg);
    friend EnchantmentRegistry compact_serial::read_enchantment_registry(ByteStreamReader& r);

    std::vector<EnchInfo> instances_;
    std::unordered_map<std::string, int32_t> name_to_index_;
    std::unordered_map<int32_t, std::unordered_set<int32_t>> incompatible_table_;

    // Subset mapping (empty for root registry)
    std::vector<int32_t> local_to_global_;
    std::unordered_map<int32_t, int32_t> global_to_local_;
};
