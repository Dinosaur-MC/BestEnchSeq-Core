#pragma once
#include "Ench.h"
#include <cstdint>
#include <unordered_set>

class EnchSet : public std::unordered_set<Ench, Ench::Hash> {
  private:
    struct Cache {
        std::unordered_set<int32_t> incompatible;
    } mutable _cache;

  public:
    using std::unordered_set<Ench, Ench::Hash>::unordered_set;

    void update_cache() const;
    const Cache &get_cache() const;

    bool is_incompatible(const int32_t e) const;
    bool is_incompatible_s(const int32_t e) const;
    EnchSet combine(const EnchSet &other) const;
    EnchSet combine_s(const EnchSet &other) const;
    int32_t combine(const EnchSet &other, int32_t multiplier_index);
    int32_t combine_s(const EnchSet &other, int32_t multiplier_index);
};
