#pragma once

// ====================================================================
// BestEnchSeq — Algorithm Domain
// ====================================================================
// Mathematical model types and search infrastructure.
// Zero dependencies on business/interface domains.

// ── Core Types ──
#include "domain/algorithm/types/AlgorithmTypes.h" // IWYU pragma: export
#include "domain/algorithm/types/ConfigTypes.h"    // IWYU pragma: export
#include "domain/algorithm/types/Enchantment.h"    // IWYU pragma: export
#include "domain/algorithm/types/Equipment.h"      // IWYU pragma: export
#include "domain/algorithm/types/Item.h"           // IWYU pragma: export
#include "domain/algorithm/types/Platform.h"       // IWYU pragma: export
#include "domain/algorithm/types/Solution.h"       // IWYU pragma: export
#include "domain/algorithm/types/ResolverTypes.h"  // ResolverOutput, DirectResolverInput, InventoryResolverInput  // IWYU pragma: export

// ── Core Interfaces ──
#include "domain/algorithm/AlgorithmExecutor.h" // IWYU pragma: export
#include "domain/algorithm/ExecutionContext.h"  // IWYU pragma: export
#include "domain/algorithm/IAlgorithm.h"        // IWYU pragma: export

// ── Registries ──
#include "domain/algorithm/registries/AlgorithmRegistry.h" // IWYU pragma: export
#include "domain/algorithm/registries/EnchReg.h"           // IWYU pragma: export

// ── Resolvers ──
#include "domain/algorithm/resolvers/InventoryResolver.h" // IWYU pragma: export
#include "domain/algorithm/resolvers/ItemResolver.h"      // IWYU pragma: export

// ── Plugin System ──
#include "domain/algorithm/plugin/AlgorithmLoader.h" // IWYU pragma: export
#include "domain/algorithm/plugin/PluginAPI.h"       // IWYU pragma: export
#include "domain/algorithm/plugin/PluginEntry.h"     // IWYU pragma: export

// ── Forge Engine ──
#include "domain/algorithm/forge_engine/ForgeEngine.h"  // IWYU pragma: export
#include "domain/algorithm/forge_engine/IForgeEngine.h" // IWYU pragma: export

// ── Diagnostics ──
#include "domain/algorithm/diagnostics/DiagnosticsService.h" // IWYU pragma: export

// ── Serialization ──
#include "domain/algorithm/serialization/CompactSerializer.h"    // IWYU pragma: export
#include "domain/algorithm/serialization/IAlgorithmSerializer.h" // IWYU pragma: export
