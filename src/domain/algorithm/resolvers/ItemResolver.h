#pragma once
#include "domain/algorithm/types/ResolverTypes.h"

namespace algorithm {

/// Direct-mode input preprocessing.
///   1. Computes diff = target_item.enchs - source_ench
///   2. Generates graduated books for each required level
///
/// Returns empty vector if target already satisfied (nothing to forge).
struct ItemResolver {
    static ResolverOutput resolve(const Item& target_item,
                                   const EnchSet& source_ench);
};

} // namespace algorithm
