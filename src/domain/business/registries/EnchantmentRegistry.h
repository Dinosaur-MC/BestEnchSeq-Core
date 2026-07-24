#pragma once
#include "IRegistry.h"
#include "domain/business/types/EnchInfo.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class EnchantmentRegistry : public IRegistry<EnchInfo> {
  public:
    EnchantmentRegistry() = default;
    EnchantmentRegistry(const std::vector<EnchInfo>& infos);

    /// Numeric index lookup (O(1)). Throws std::out_of_range on invalid.
    const EnchInfo &get(int32_t id) const;

    /// NSID lookup (O(1) average). Throws std::out_of_range if not found.
    const EnchInfo &get(const NSID &id) const;

    // Incompatibility table
    const std::unordered_set<NSID> &get_exclusive_set(const NSID &e) const;
    bool is_incompatible(const NSID &e1, const NSID &e2) const;

    // Validation
    static bool check_validation(const std::vector<EnchInfo> &infos);

    // ── IRegistry overrides ────────────────────────────────────────────
    iterator find(const NSID &id) override;
    const_iterator find(const NSID &id) const override;
    bool insert(const EnchInfo &item) override;
    bool remove(const NSID &id) override;
    void clear() noexcept override;

  private:
    std::unordered_map<NSID, int32_t> name_to_index_;
    std::unordered_map<NSID, std::unordered_set<NSID>> incompatible_table_;
};
