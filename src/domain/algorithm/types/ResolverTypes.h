#pragma once
#include "domain/algorithm/types/Item.h"

namespace algorithm {

/// Output of any resolver.  Empty vector means "unreachable / no work needed".
using ResolverOutput = ItemCollection;

} // namespace algorithm
