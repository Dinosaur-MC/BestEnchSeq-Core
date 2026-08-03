#pragma once
#include "domain/algorithm/resolvers/IResolver.h"

namespace algorithm {

/// Vanilla resolver.
///   direct:    generates the diff books from the source (former ItemResolver).
///   inventory: runs a cost-aware selection pass over the available pool —
///              conflict-aware best-base pick (min est gap cost), diff-aware
///              book filtering, minimal equipment retention (books-can't-reach
///              enchants), and (level desc, ppn asc) output ordering.  Priority
///              from the business payload is consumed only as the initial
///              pre-sort key (the compact Item carries no priority).
/// Subclass for custom preprocessing.
class DefaultResolver : public IResolver {
  public:
    ResolverOutput resolve(const AlgorithmInput &input) const override;
};

} // namespace algorithm
