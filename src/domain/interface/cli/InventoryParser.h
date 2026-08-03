#pragma once
#include "InventorySchema.h"
#include "domain/business/types/Item.h"
#include <string>
#include <vector>

class EnchantmentRegistry;
class EquipmentRegistry;

/// Parsed inventory task: target item + available items and their forge
/// priorities, plus optional algorithm/profile overrides from the file.
struct InventoryInput {
    Item target_item; // JSON `target` → domain Item; default-empty if the item string is empty
    ItemCollection items;
    std::vector<int32_t> priorities; // parallel to items; absent → 99
    std::string algorithm;           // empty = not specified
    std::string profile;             // empty = not specified
};

struct InventoryParser {
    /// Parse an inventory JSON task into a target item + available items.
    ///
    /// Self-contained file format:
    ///   { "target": { "item": "diamond_sword",
    ///                 "enchants": [ { "id": "sharpness", "level": 5 }, ... ] },
    ///     "items": [ { "type": "book"|"equipment", "id": "...",
    ///                  "enchants": [ { "id": "...", "level": N }, ... ],
    ///                  "prior_penalty": N, "durability": N, "priority": N },
    ///                ... ],
    ///     "algorithm": "dp_merge", "profile": "..." }
    ///
    /// Structural parse is schema-driven (ds::, see InventorySchema.h);
    /// enchantment/equipment ids and level/durability bounds are validated
    /// against the given registries.  `target` is required for a full task;
    /// `algorithm`/`profile` are optional.  Unknown root keys (e.g. old
    /// decorative name/description/author/version) are tolerated.
    ///
    /// Throws std::runtime_error on file read / parse errors, unknown
    /// enchantments, enchantment levels exceeding the registry max level,
    /// unknown equipment, or malformed entries.
    static InventoryInput
    parse_file(const std::string& path, const EnchantmentRegistry& ench_reg, const EquipmentRegistry& eq_reg);

    /// Shared parse core. `path == "-"` in parse_file reads stdin and
    /// delegates here.
    static InventoryInput
    parse_string(const std::string& content, const EnchantmentRegistry& ench_reg, const EquipmentRegistry& eq_reg);

    // ── Two-phase split (CLI --input wiring) ──
    //
    // Phase 1 (no registry): structural ds parse only.  The caller may switch
    // the active profile based on `dto.profile` BEFORE cross-validating, so a
    // self-contained task can reference enchantments/equipment from a profile
    // other than the currently-active one.
    static InvTaskDto parse_task(const std::string& content);

    /// Phase 2: registry cross-validation + domain Item construction from a
    /// structurally-parsed DTO.  `parse_string` is this followed by Phase 1.
    static InventoryInput
    build_inventory(const InvTaskDto& dto, const EnchantmentRegistry& ench_reg, const EquipmentRegistry& eq_reg);

    /// Read task content from a file path, or stdin when `path == "-"`.
    static std::string read_content(const std::string& path);
};
