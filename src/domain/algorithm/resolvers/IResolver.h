#pragma once
#include "domain/algorithm/types/ResolverTypes.h"

namespace algorithm {

struct AlgorithmInput;

/// Unified input-resolver interface (mirrors the IForgeEngine ownership
/// pattern).  Turns an AlgorithmInput into the full working item set the
/// strategy searches over.  Algorithms own a resolver via get_resolver()
/// and call it inside execute()/simulate().
class IResolver {
  public:
    virtual ~IResolver() noexcept = default;

    /// Full working item set for the strategy:
    ///   direct:    [base equipment (target + source), ...generated books]
    ///   inventory: the priority-sorted available pool (no equipment-first
    ///              guarantee — strategies select their own base via
    ///              Item::type).  Empty = unreachable / no work needed.
    virtual ResolverOutput resolve(const AlgorithmInput &input) const = 0;
};

} // namespace algorithm
