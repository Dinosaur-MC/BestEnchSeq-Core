#pragma once

#include "registries/AlgorithmRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"

/// Namespace-level global access to singleton registry instances.
///
/// Each function owns a `static` local instance by value (Meyer's singleton).
/// The parser and output layers access registries through these free functions
/// instead of calling XxxRegistry::get_instance().
///
/// Tests that need isolated registries construct local instances directly
/// and pass them through parameter-based interfaces (CompactAdapter, etc.).
namespace registries {

inline EnchantmentRegistry& enchants() {
    static EnchantmentRegistry instance;
    return instance;
}

inline EquipmentCategoryRegistry& categories() {
    static EquipmentCategoryRegistry instance;
    return instance;
}

inline EquipmentRegistry& equipment() {
    static EquipmentRegistry instance;
    return instance;
}

inline AlgorithmRegistry& algorithms() {
    static AlgorithmRegistry instance;
    return instance;
}

} // namespace registries
