#pragma once
#include "ds/ds.h"
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

// ============================================================================
// Inventory schema layer — pure DTO structs + ds:: schemas.
//
// Self-contained inventory JSON (target + items + algorithm + profile):
//   {
//     "profile": "modded_sword",
//     "target": { "item": "diamond_sword",
//                 "enchants": [ {"id": "sharpness", "level": 5}, ... ] },
//     "items": [ { "type": "book"|"equipment", "id": "...",
//                  "enchants": [ { "id": "...", "level": N }, ... ],
//                  "prior_penalty": N, "priority": N, "durability": N }, ... ],
//     "algorithm": "dp_merge"
//   }
//
// This header has NO registry/business dependencies — it only performs the
// structural ds:: parse.  Registry cross-validation (known enchantments,
// equipment ids, level bounds, durability) happens in InventoryParser.
// ds tolerates unknown root keys (Strict=false), so old files carrying
// decorative name/description/author/version fields still parse.
// ============================================================================

/// One enchantment entry: { "id", "level" }.
struct InvEnchDto {
    std::string id;
    int32_t level = 1;
};
struct InvEnchSchema {
    using Type = InvEnchDto;
    static constexpr auto fields = std::tuple{
        ds::required_field("id", &Type::id, ds::string_codec{}),
        ds::field("level", &Type::level, ds::int_codec{}),
    };
};

/// One inventory item entry: { "type", "id", "enchants", "prior_penalty",
/// "priority", "durability" }.
struct InvItemDto {
    std::string type; // "book" | "equipment"
    std::string id;   // equipment only
    std::vector<InvEnchDto> enchants;
    int32_t prior_penalty = 0;
    int32_t priority = 99;
    int32_t durability = 0; // 0 → max (resolved in cross-validation)
};
struct InvItemSchema {
    using Type = InvItemDto;
    static constexpr auto fields = std::tuple{
        ds::required_field("type", &Type::type, ds::string_codec{}),
        ds::field("id", &Type::id, ds::string_codec{}),
        ds::field("enchants", &Type::enchants, ds::vector_codec<ds::object_codec<InvEnchSchema>>{}),
        ds::field("prior_penalty", &Type::prior_penalty, ds::int_codec{}),
        ds::field("priority", &Type::priority, ds::int_codec{}),
        ds::field("durability", &Type::durability, ds::int_codec{}),
    };
};

/// Target specification: { "item", "enchants" }.  `item` is the equipment id
/// or "book"/"enchanted_book"; an empty item string means "no target".
struct InvTargetDto {
    std::string item; // equipment id or "book"/"enchanted_book"
    std::vector<InvEnchDto> enchants;
};
struct InvTargetSchema {
    using Type = InvTargetDto;
    static constexpr auto fields = std::tuple{
        ds::required_field("item", &Type::item, ds::string_codec{}),
        ds::field("enchants", &Type::enchants, ds::vector_codec<ds::object_codec<InvEnchSchema>>{}),
    };
};

/// Top-level task: { "target", "items", "algorithm", "profile" }.
/// `target` is required for a full task; `algorithm`/`profile` are optional.
struct InvTaskDto {
    InvTargetDto target;
    std::vector<InvItemDto> items;
    std::string algorithm; // empty = not specified
    std::string profile;   // empty = not specified
};
struct InvTaskSchema {
    using Type = InvTaskDto;
    static constexpr auto fields = std::tuple{
        ds::required_field("target", &Type::target, ds::object_codec<InvTargetSchema>{}),
        ds::field("items", &Type::items, ds::vector_codec<ds::object_codec<InvItemSchema>>{}),
        ds::field("algorithm", &Type::algorithm, ds::string_codec{}),
        ds::field("profile", &Type::profile, ds::string_codec{}),
    };
};

using InvTaskJson = ds::json::Schema<InvTaskSchema>;
