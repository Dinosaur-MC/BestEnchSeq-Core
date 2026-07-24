#pragma once
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"
#include "common/io/json.h"

#include <string>
#include <vector>

/// Parser for the native JSON format (vanilla.json).
///
/// The native JSON format is an all-in-one file with "enchantments",
/// "equipments", and "tags" top-level keys.
class NativeJsonParser {
public:
    /// Parse from a pre-parsed Json DOM.
    static std::pair<std::vector<EnchantmentData>, std::vector<EquipmentData>>
    parse(const Json& json);

    /// Parse from a JSON string.
    static std::pair<std::vector<EnchantmentData>, std::vector<EquipmentData>>
    parse_string(const std::string& content);
};
