#pragma once

// ====================================================================
// BestEnchSeq — Business Domain
// ====================================================================
// Game-concept domain model: enchantment definitions, equipment specs,
// item stacks, profiles, solutions, and their registries.
//
// Self-contained — depends only on common/.

// ── Types ──
#include "domain/business/types/Ench.h"         // IWYU pragma: export
#include "domain/business/types/EnchInfo.h"     // IWYU pragma: export
#include "domain/business/types/EnchSet.h"      // IWYU pragma: export
#include "domain/business/types/Equipment.h"    // IWYU pragma: export
#include "domain/business/types/EquipmentTag.h" // IWYU pragma: export
#include "domain/business/types/Item.h"         // IWYU pragma: export
#include "domain/business/types/Solution.h"     // IWYU pragma: export
#include "domain/business/types/Profile.h"      // IWYU pragma: export

// ── DTO Types ──
#include "domain/business/types/dto/EnchantmentData.h" // IWYU pragma: export
#include "domain/business/types/dto/EquipmentData.h"   // IWYU pragma: export

// ── Registries ──
#include "domain/business/registries/IRegistry.h"            // IWYU pragma: export
#include "domain/business/registries/EnchantmentRegistry.h"  // IWYU pragma: export
#include "domain/business/registries/EquipmentRegistry.h"    // IWYU pragma: export
#include "domain/business/registries/EquipmentTagRegistry.h" // IWYU pragma: export

// ── Loaders ──
#include "domain/business/loaders/RegistryLoader.h"  // IWYU pragma: export
#include "domain/business/loaders/ProfileLoader.h"   // IWYU pragma: export

// ── Managers ──
#include "domain/business/managers/RegistryManager.h"  // IWYU pragma: export
#include "domain/business/managers/ProfileManager.h"   // IWYU pragma: export

// ── Components ──
#include "domain/business/components/Serializer.h"    // IWYU pragma: export
#include "domain/business/components/FormatDetector.h" // IWYU pragma: export
