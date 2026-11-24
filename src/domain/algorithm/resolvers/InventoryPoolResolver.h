#pragma once
#include "domain/algorithm/resolvers/IResolver.h"
#include <cstddef>

namespace algorithm {

/// Candidate-pool resolver for the exact strategies (dp_merge / bb_dp).
///
/// Direct mode: identical to DefaultResolver.
///
/// Inventory mode: DefaultResolver's heuristic selection (best base by
/// estimate, one cheapest book per gap, irreplaceable equipment) is a good
/// starting point but often not the cheapest set — a different base, a book
/// carrying several target enchants, or an enchanted equipment as sacrifice can
/// beat it.  Exact strategies search every subset of what they are given and
/// keep the cheapest target-meeting one, so they are handed a wider pool:
///
///   pool = DefaultResolver selection
///        + further relevant items (equipment; books carrying a target
///          enchantment), best-scored first, up to kPoolMax items in total.
///
/// The selection is always included, so the exact optimum over the pool is
/// never worse than over the selection.  kPoolMax keeps the exact search
/// interactive (≈ 0.4 s worst case up to 14 items in measurements); a selection
/// that is already larger is returned unchanged.
class InventoryPoolResolver : public IResolver {
public:
    static constexpr size_t kPoolMax = 14;

    ResolverOutput resolve(const AlgorithmInput& input) const override;
};

} // namespace algorithm
