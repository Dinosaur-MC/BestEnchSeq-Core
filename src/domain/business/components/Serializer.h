#pragma once
#include "domain/business/types/Ench.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/EnchSet.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentCategoryRegistry.h"
#include "common/io/json.h"

#include <string>

// ══════════════════════════════════════════════════════════════════════════
// Business Domain JSON Serialization — operator<< / operator>>
//
// Follows the same pattern as ByteStream: << serializes (business → Json),
// >> deserializes (Json → business).
//
// All operators are found via ADL on Json (global namespace).
//
// Usage:
//   Json j;  j << some_ench;        // Ench → Json
//   Ench e;  j >> e;                // Json → Ench
//
//   Json reg_json;  reg_json << reg; // EnchantmentRegistry → Json array
//   EnchantmentRegistry reg;  json >> reg;  // Json → EnchantmentRegistry
// ══════════════════════════════════════════════════════════════════════════

// ── Ench ──
Json& operator<<(Json& json, const Ench& ench);
const Json& operator>>(const Json& json, Ench& ench);

// ── EnchInfo ──
Json& operator<<(Json& json, const EnchInfo& info);
const Json& operator>>(const Json& json, EnchInfo& info);

// ── EnchSet ──
Json& operator<<(Json& json, const EnchSet& set);
const Json& operator>>(const Json& json, EnchSet& set);

// ── EquipmentCategory ──
Json& operator<<(Json& json, const EquipmentCategory& cat);
const Json& operator>>(const Json& json, EquipmentCategory& cat);

// ── Equipment ──
Json& operator<<(Json& json, const Equipment& eq);
const Json& operator>>(const Json& json, Equipment& eq);

// ── Item ──
Json& operator<<(Json& json, const Item& item);
const Json& operator>>(const Json& json, Item& item);

// ── Solution::EnchStep ──
Json& operator<<(Json& json, const Solution::EnchStep& step);
const Json& operator>>(const Json& json, Solution::EnchStep& step);

// ── Solution::MetaData ──
Json& operator<<(Json& json, const Solution::MetaData& meta);
const Json& operator>>(const Json& json, Solution::MetaData& meta);

// ── Solution ──
Json& operator<<(Json& json, const Solution& sol);
const Json& operator>>(const Json& json, Solution& sol);

// ── EnchantmentRegistry ──
Json& operator<<(Json& json, const EnchantmentRegistry& reg);
const Json& operator>>(const Json& json, EnchantmentRegistry& reg);

// ── EquipmentRegistry ──
Json& operator<<(Json& json, const EquipmentRegistry& reg);
const Json& operator>>(const Json& json, EquipmentRegistry& reg);

// ── EquipmentCategoryRegistry ──
Json& operator<<(Json& json, const EquipmentCategoryRegistry& reg);
const Json& operator>>(const Json& json, EquipmentCategoryRegistry& reg);

// ══════════════════════════════════════════════════════════════════════════
// Serializer — utility methods (MCE helpers + string-level convenience)
// ══════════════════════════════════════════════════════════════════════════

struct Serializer {
    // ── MCE platform enum helpers ──
    static std::string_view mce_to_string(MCE platform) noexcept;
    static MCE string_to_mce(std::string_view str) noexcept;

    // ── Convenience: render any JSON to a string, or parse from string ──
    static std::string to_string(const Json& json, Json::JsonStyle style = Json::Pretty);
    static Json from_string(const std::string& str);
};
