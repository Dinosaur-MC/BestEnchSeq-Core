#include "EnchSet.h"
#include <algorithm>

EnchSet::iterator EnchSet::find(const NSID &ench_id) {
    return std::find_if(begin(), end(), [&](const Ench &e) { return e.id == ench_id; });
}
EnchSet::const_iterator EnchSet::find(const NSID &ench_id) const {
    return std::find_if(begin(), end(), [&](const Ench &e) { return e.id == ench_id; });
}
