#pragma once
#include <cstdint>

namespace platform {

enum MCE : int8_t {
    None = 0x00,
    Java = 0x01,
    Bedrock = 0x02,
    All = 0x03,
};

} // namespace platform

// EquipmentCategory has been moved to types/EquipmentCategory.h
// - Now uses numeric IDs managed by EquipmentCategoryRegistry
// - No longer inherits std::string or has virtual destructor
// - Include path: #include "types/EquipmentCategory.h"
// - Header provides std::hash<EquipmentCategory> specialization
