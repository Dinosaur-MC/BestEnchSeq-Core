#pragma once
#include "Ench.h"

// ─── Enchantment set with combining rules ───
//
// Cache responsibility: `_cache` tracks incompatible-enchant IDs for O(1)
// lookup. Methods without `_s` suffix read the existing cache (which may be
// stale if the set was recently mutated). Call `update_cache()` or use the
// `_s` variants when cache freshness is required. The types layer never
// updates the cache implicitly on mutation — that is the caller's decision.
//
// Thread safety: NOT safe for concurrent read/write. A single `EnchSet`
// must not be mutated or read from multiple threads simultaneously. Callers
// that share sets across threads must provide their own synchronization.

class EnchSet : public std::unordered_set<Ench, Ench::Hash> {
  private:
    struct Cache {
        std::unordered_set<int32_t> incompatible;
    } mutable _cache;

  public:
    using std::unordered_set<Ench, Ench::Hash>::unordered_set;
    virtual ~EnchSet() = default;

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
