#pragma once

// ====================================================================
// BestEnchSeq — Business Domain
// ====================================================================
// Game-concept domain model: enchantment definitions, equipment specs,
// item stacks, solutions, and their registries.

// ── Types ──
#include "domain/business/types/Ench.h"           // IWYU pragma: export
#include "domain/business/types/EnchInfo.h"       // IWYU pragma: export
#include "domain/business/types/EnchSet.h"        // IWYU pragma: export
#include "domain/business/types/EquipmentTag.h"   // IWYU pragma: export
#include "domain/business/types/Equipment.h"      // IWYU pragma: export
#include "domain/business/types/Item.h"           // IWYU pragma: export
#include "domain/business/types/Solution.h"       // IWYU pragma: export

// ── Components ──
#include "domain/business/components/Serializer.h"                 // IWYU pragma: export

// ── Registries ──
#include "domain/business/registries/EnchantmentRegistry.h"        // IWYU pragma: export
#include "domain/business/registries/EquipmentRegistry.h"          // IWYU pragma: export
#include "domain/business/registries/EquipmentTagRegistry.h"  // IWYU pragma: export
#include "domain/business/registries/RegistryManager.h"            // IWYU pragma: export
