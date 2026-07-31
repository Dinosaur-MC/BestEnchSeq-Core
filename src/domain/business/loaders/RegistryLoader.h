#pragma once
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"
#include "common/io/json.h"

#include <vector>

/// Low-level conversion between DTOs and business registries.
///
/// Absorbs the logic of the former RawTypeAdapter:
///   - DTO → Registry: resolves string IDs → NSID-based registry entries
///   - Registry → DTO: inverse conversion for export
///
/// Normally used internally by ProfileLoader rather than directly.
class RegistryLoader {
public:
    // ── DTO → Registry ────────────────────────────────────────────────

    /// Convert EnchantmentData[] → EnchantmentRegistry.
    /// Resolves category name strings to NSID tag references.
    /// Resolves exclusive_with strings to namespaced NSIDs.
    void from_dto(EnchantmentRegistry& reg,
                  const EquipmentTagRegistry& tag_reg,
                  const std::vector<business::loader::EnchantmentData>& data);

    /// Convert EquipmentData[] → EquipmentRegistry.
    /// Resolves category strings to NSID tag references.
    void from_dto(EquipmentRegistry& reg,
                  const EquipmentTagRegistry& tag_reg,
                  const std::vector<business::loader::EquipmentData>& data);

    // ── Json → Registry ───────────────────────────────────────────────

    bool from_json(EnchantmentRegistry& reg, const Json& json);
    bool from_json(EquipmentRegistry& reg, const Json& json);
    bool from_json(EquipmentTagRegistry& reg, const Json& json);

    // ── Registry → Json ───────────────────────────────────────────────

    Json to_json(const EnchantmentRegistry& reg);
    Json to_json(const EquipmentRegistry& reg);
    Json to_json(const EquipmentTagRegistry& reg);

    // ── Registry → DTO ────────────────────────────────────────────────

    std::vector<business::loader::EnchantmentData> to_dto(
        const EnchantmentRegistry& reg,
        const EquipmentTagRegistry& tag_reg);

    std::vector<business::loader::EquipmentData> to_dto(
        const EquipmentRegistry& reg,
        const EquipmentTagRegistry& tag_reg);

    // ── Full pipeline (tag_reg → eq_reg → ench_reg) ───────────────────

    /// Three-step resolve: build tag_reg from equipment data, then
    /// populate eq_reg and ench_reg with resolved IDs.
    ///
    /// \p base_tags (optional) seeds the tag registry first — used as a
    /// vanilla fallback so a mod profile's `applicable_equipment` references
    /// resolve against categories the profile itself does not define.  Pass
    /// nullptr to build the registry from scratch.
    void resolve(
        const std::vector<business::loader::EnchantmentData>& enchants,
        const std::vector<business::loader::EquipmentData>& equipments,
        EquipmentTagRegistry& tag_reg,
        EquipmentRegistry& eq_reg,
        EnchantmentRegistry& ench_reg,
        const EquipmentTagRegistry* base_tags = nullptr);
};
