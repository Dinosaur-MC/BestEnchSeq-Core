#pragma once
#include "Ench.h"
#include <unordered_set>

/// Enchantment set — pure data container.
/// `operator==` compares all members for strict equality.
/// Use `find_by_id()` for id-only lookups.
class EnchSet : public std::unordered_set<Ench, Ench::Hash> {
  public:
    using std::unordered_set<Ench, Ench::Hash>::unordered_set;

    iterator find_by_id(int32_t id);
    const_iterator find_by_id(int32_t id) const;
};
