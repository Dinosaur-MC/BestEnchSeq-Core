#pragma once

// ====================================================================
// BestEnchSeq — Orchestration Layer
// ====================================================================
// Cross-domain adapters, serializers, and formatters.
// Bridges business/interface domain types and algorithm compact types.

#include "components/CompactAdapter.h"   // business ⇄ algorithm type conversion
#include "components/EnchSerializer.h"   // Enchantment/equipment serialization (JSON/CSV)
#include "components/OutputFormatter.h"  // Solution formatting (verbose/compact/json)
#include "components/RawTypeAdapter.h"   // Raw (string-based) ⇄ domain type bridging
