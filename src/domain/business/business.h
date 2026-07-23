#pragma once

// ====================================================================
// BestEnchSeq — Business Domain
// ====================================================================
// Game-concept domain model: enchantment definitions, equipment specs,
// item stacks, solutions, and their registries.

// ── Types ──
#include "types/Enchantment.h"    // Ench, EnchInfo, EnchSet
#include "types/Equipment.h"      // Equipment
#include "types/Item.h"           // Item, ItemCollection
#include "types/Solution.h"       // Solution, EnchStep, MetaData

// ── Registries ──
#include "registries/EnchantmentRegistry.h"        // Full enchantment registry
#include "registries/EquipmentRegistry.h"          // Equipment definitions
#include "registries/EquipmentCategoryRegistry.h"  // Category ID mapping
#include "registries/RegistryManager.h"            // Multi-registry CRUD
