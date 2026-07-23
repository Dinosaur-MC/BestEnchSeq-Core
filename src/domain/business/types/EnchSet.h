#pragma once
#include "Ench.h"
#include <unordered_set>

class EnchSet : public std::unordered_set<Ench> {
  public:
    using std::unordered_set<Ench>::unordered_set;

    iterator find(const NSID& ench_id);
    const_iterator find(const NSID& ench_id) const;
};
