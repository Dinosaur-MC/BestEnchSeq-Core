#pragma once

// ====================================================================
// BestEnchSeq — Algorithm Domain
// ====================================================================
// Mathematical model types and search infrastructure.
// Zero dependencies on business/interface domains.
//
// This umbrella exports only the external-facing API.
// Internal sub-modules (resolvers, serialization, components,
// strategies) are NOT re-exported.

// ── Core Types ──
#include "domain/algorithm/types/AlgorithmTypes.h" // IWYU pragma: export
#include "domain/algorithm/types/ConfigTypes.h"    // IWYU pragma: export
#include "domain/algorithm/types/Enchantment.h"    // IWYU pragma: export
#include "domain/algorithm/types/Equipment.h"      // IWYU pragma: export
#include "domain/algorithm/types/Item.h"           // IWYU pragma: export
#include "domain/algorithm/types/Solution.h"       // IWYU pragma: export

// ── Core Interfaces ──
#include "domain/algorithm/AlgorithmExecutor.h"              // IWYU pragma: export
#include "domain/algorithm/ExecutionContext.h"               // IWYU pragma: export
#include "domain/algorithm/IAlgorithm.h"                     // IWYU pragma: export
#include "domain/algorithm/IExecutor.h"                      // IWYU pragma: export
#include "domain/algorithm/diagnostics/IAlgorithmObserver.h" // IWYU pragma: export

// ── Sandbox ──
#include "domain/algorithm/sandbox/SandboxedExecutor.h" // IWYU pragma: export

// ── Registries ──
#include "domain/algorithm/registries/AlgorithmRegistry.h" // IWYU pragma: export
#include "domain/algorithm/registries/EnchReg.h"           // IWYU pragma: export

// ── Plugin System ──
#include "domain/algorithm/plugin/AlgorithmLoader.h" // IWYU pragma: export
