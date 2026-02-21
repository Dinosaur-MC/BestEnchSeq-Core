#pragma once
#include "Ench.h"

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
    int32_t combine(const EnchSet &other, bool is_book);
    int32_t combine_s(const EnchSet &other, bool is_book);
    std::pair<EnchSet, int32_t> combine(const EnchSet &other, bool is_book) const;
    std::pair<EnchSet, int32_t> combine_s(const EnchSet &other, bool is_book) const;
};
