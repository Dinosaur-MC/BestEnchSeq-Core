#pragma once
#include "Ench.h"
#include <unordered_set>

/// Enchantment set — pure data container, no computational logic.
class EnchSet : public std::unordered_set<Ench, Ench::Hash> {
  public:
    using std::unordered_set<Ench, Ench::Hash>::unordered_set;
};
