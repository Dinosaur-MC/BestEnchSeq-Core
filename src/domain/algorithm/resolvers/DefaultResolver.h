#pragma once
#include "domain/algorithm/resolvers/IResolver.h"

namespace algorithm {

/// Vanilla resolver — merges the former ItemResolver (direct book generation)
/// and InventoryResolver (priority sort + feasibility).  Subclass for custom
/// preprocessing.
class DefaultResolver : public IResolver {
  public:
    ResolverOutput resolve(const AlgorithmInput &input) const override;
};

} // namespace algorithm
