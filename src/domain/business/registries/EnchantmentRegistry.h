#pragma once
#include "IRegistry.h"
#include "domain/business/types/EnchInfo.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

class EnchantmentRegistry : public IRegistry<EnchInfo> {
  public:
    EnchantmentRegistry() = default;
    EnchantmentRegistry(const std::vector<EnchInfo>& infos);

    // Incompatibility table
    const std::unordered_set<NSID>& get_exclusive_set(const NSID& e) const;
    bool is_incompatible(const NSID& e1, const NSID& e2) const;

    // Validation
    static bool check_validation(const std::vector<EnchInfo>& infos);

    // ── IRegistry overrides ────────────────────────────────────────────
    std::pair<iterator, bool> insert(const EnchInfo& item) override;
    bool erase(const NSID& id) override;
    void clear() noexcept override;

  private:
    // NSID → set of incompatible NSIDs (bidirectional, built from exclusive_set)
    std::unordered_map<NSID, std::unordered_set<NSID>> incompatible_table_;
};
