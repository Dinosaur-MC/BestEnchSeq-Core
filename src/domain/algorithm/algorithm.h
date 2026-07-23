#pragma once

// ====================================================================
// BestEnchSeq — Algorithm Domain
// ====================================================================
// Mathematical model types and search infrastructure.
// Zero dependencies on business/interface domains.

// ── Core Types ──
#include "types/Enchantment.h"       // Ench, EnchSet (compact)
#include "types/Equipment.h"         // Equipment
#include "types/Item.h"              // Item, ItemType, ItemCollection
#include "types/Solution.h"          // Solution, Step, AlgorithmOutput
#include "types/AlgorithmTypes.h"    // AlgorithmInput, AlgorithmMode, ResolvedInput
#include "types/ConfigTypes.h"       // ForgeConfig, SearchConfig
#include "types/Platform.h"          // Platform (MCE wrapper)

// ── Core Interfaces ──
#include "IAlgorithm.h"              // IAlgorithm interface
#include "AlgorithmExecutor.h"       // Async executor
#include "ExecutionContext.h"        // Cancel/pause/progress

// ── Registries ──
#include "registries/EnchReg.h"      // Compact registry with O(1) conflict matrix
#include "registries/AlgorithmRegistry.h"  // String-keyed algorithm factory

// ── Resolvers ──
#include "resolvers/ItemResolver.h"      // Direct mode (diff + graduated books)
#include "resolvers/InventoryResolver.h" // Inventory mode (feasibility + sort)

// ── Plugin System ──
#include "plugin/AlgorithmLoader.h"  // Dynamic shared-library loading
#include "plugin/PluginAPI.h"        // Plugin interface types
#include "plugin/PluginEntry.h"      // BESQ_PLUGIN_ENTRY macro

// ── Forge Engine ──
#include "forge_engine/IForgeEngine.h"   // Virtual forge sub-operation interface
#include "forge_engine/ForgeEngine.h"    // Vanilla implementation

// ── Diagnostics ──
#include "diagnostics/DiagnosticsService.h"  // Event-driven diagnostics pipeline

// ── Serialization ──
#include "serialization/IAlgorithmSerializer.h"  // Serialization interface
#include "serialization/CompactSerializer.h"     // Binary checkpoint format

// ── Algorithm Components ──
#include "components/Heuristic.h"    // Search heuristic interface
#include "components/ItemPool.h"     // Pool for algorithm items
#include "components/StateHash.h"    // State hashing utilities
#include "components/SearchUtils.h"  // Search utility functions
