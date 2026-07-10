#include "parser/InputParser.h"
#include "parser/ParserUtils.h"
#include "io/json.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/RegistryAccess.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

// ---------------------------------------------------------------------------
// Resolve an EnchantmentSpec to an integer ench_id via the global
// EnchantmentRegistry.  The key is constructed as "ns:id" unless the id
// itself already contains a namespace colon.  Falls back to the bare id
// for data registered without a namespace prefix.
// ---------------------------------------------------------------------------
int32_t resolve_ench_id(const EnchantmentSpec &spec) {
    std::string namespaced = spec.id.find(':') != std::string::npos
                                 ? spec.id
                                 : spec.ns + ":" + spec.id;
    int32_t id = registries::enchants().get_id(namespaced);
    if (id >= 0) return id;

    // Fallback: try bare id (for data registered without namespace prefix)
    id = registries::enchants().get_id(spec.id);
    if (id >= 0) return id;

    throw std::runtime_error("Unknown enchantment: " + namespaced);
}

// ---------------------------------------------------------------------------
// Resolve a plain enchantment name (from JSON) to an integer ench_id via the
// global EnchantmentRegistry.  Tries the raw name first, then prepends
// "minecraft:" as a fallback.
// ---------------------------------------------------------------------------
int32_t resolve_ench_id(const std::string &name) {
    int32_t id = registries::enchants().get_id(name);
    if (id < 0) {
        id = registries::enchants().get_id("minecraft:" + name);
    }
    return id;
}

} // anonymous namespace

// ===========================================================================
//  parse_inventory
// ===========================================================================
ItemCollection InputParser::parse_inventory(
    const std::filesystem::path &path,
    const std::unordered_map<std::string, const Equipment*> &equipment_registry
) {
    std::string content = ParserUtils::read_file(path);
    Json root = Json::parse(content);

    if (root.type() != JsonType::Object) {
        return {};
    }

    // Grab the root object as a concrete value to avoid dangling-reference risks
    Json::Value root_val = root.get_value();
    if (!std::holds_alternative<Json::Object>(root_val)) {
        return {};
    }
    Json::Object root_obj = std::get<Json::Object>(root_val);

    auto items_it = root_obj.find("items");
    if (items_it == root_obj.end()) {
        return {};
    }

    Json::Value items_val = items_it->second.get_value();
    if (!std::holds_alternative<Json::Array>(items_val)) {
        return {};
    }
    Json::Array items_arr = std::get<Json::Array>(items_val);
    ItemCollection result;

    for (const Json &item_json : items_arr) {
        if (item_json.type() != JsonType::Object) {
            continue;
        }

        Json::Value item_val = item_json.get_value();
        if (!std::holds_alternative<Json::Object>(item_val)) {
            continue;
        }
        Json::Object item_obj = std::get<Json::Object>(item_val);

        std::string type = ParserUtils::get_json_string(item_obj, "type");

        if (type.empty()) {
            std::cerr << "Warning: item missing 'type' field, skipping" << std::endl;
            continue;
        }

        // ---- Parse enchantments -------------------------------------------
        EnchSet ench_set;
        auto ench_it = item_obj.find("enchants");
        if (ench_it != item_obj.end()) {
            Json::Value enchants_val = ench_it->second.get_value();
            if (std::holds_alternative<Json::Array>(enchants_val)) {
                Json::Array ench_arr = std::get<Json::Array>(enchants_val);
                for (const Json &ench_json : ench_arr) {
                    if (ench_json.type() != JsonType::Object) {
                        continue;
                    }
                    Json::Value ench_val = ench_json.get_value();
                    if (!std::holds_alternative<Json::Object>(ench_val)) {
                        continue;
                    }
                    Json::Object ench_obj = std::get<Json::Object>(ench_val);

                    std::string ench_id_str = ParserUtils::get_json_string(ench_obj, "id");
                    int32_t ench_level = ParserUtils::get_json_int(ench_obj, "level");
                    if (ench_level < 1) ench_level = 1;

                    int32_t ench_id = resolve_ench_id(ench_id_str);
                    if (ench_id >= 0) {
                        ench_set.emplace(ench_id, ench_level);
                    } else {
                        std::cerr << "Warning: unknown enchantment '" << ench_id_str
                                  << "' in item, skipping" << std::endl;
                    }
                }
            }
        }

        int32_t prior_penalty = ParserUtils::get_json_int(item_obj, "prior_penalty");
        int32_t priority = ParserUtils::get_json_int(item_obj, "priority");
        if (priority <= 0) priority = 99;

        // ---- Dispatch by type ---------------------------------------------
        if (type == "book") {
            auto &item = result.emplace_back(ench_set, prior_penalty);
            item.priority = priority;
        } else if (type == "equipment") {
            std::string equip_id = ParserUtils::get_json_string(item_obj, "id");
            auto equip_it = equipment_registry.find(equip_id);
            const Equipment *equip =
                (equip_it != equipment_registry.end()) ? equip_it->second : nullptr;

            if (equip != nullptr) {
                int32_t durability = ParserUtils::get_json_int(item_obj, "durability");
                if (durability <= 0) {
                    durability = equip->max_durability;
                }
                auto &item = result.emplace_back(*equip, ench_set, prior_penalty, durability);
                item.priority = priority;
            } else {
                // Equipment not found in registry, treat as book-like
                std::cerr << "Warning: equipment '" << equip_id
                          << "' not found in registry, treating as book" << std::endl;
                result.emplace_back(ench_set, prior_penalty);
            }
        } else {
            std::cerr << "Warning: unknown item type '" << type << "', skipping" << std::endl;
        }
    }

    return result;
}

// ===========================================================================
//  build_target
// ===========================================================================
ItemStack InputParser::build_target(
    const TargetSpec &target_spec,
    const std::unordered_map<std::string, const Equipment*> &equipment_registry
) {
    // Look up equipment
    auto equip_it = equipment_registry.find(target_spec.item_id);
    if (equip_it == equipment_registry.end()) {
        throw std::runtime_error("Unknown equipment: " + target_spec.item_id);
    }

    // Build enchantment set from inline enchants
    EnchSet ench_set;
    for (const auto &spec : target_spec.inline_enchants) {
        int32_t id = resolve_ench_id(spec);
        ench_set.emplace(id, spec.level);
    }

    return ItemStack(*equip_it->second, ench_set, 0);
}

// ===========================================================================
//  build_wanted_enchset
// ===========================================================================
EnchSet InputParser::build_wanted_enchset(
    const std::vector<EnchantmentSpec> &wanted
) {
    EnchSet result;
    for (const auto &spec : wanted) {
        int32_t id = resolve_ench_id(spec);
        result.emplace(id, spec.level);
    }
    return result;
}

// ===========================================================================
//  generate_books  (private)
// ===========================================================================
ItemCollection InputParser::generate_books(
    const EnchSet &wanted,
    const EnchSet &existing
) {
    ItemCollection books;
    for (const Ench &wanted_ench : wanted) {
        auto it = existing.find_by_id(wanted_ench.id);
        int32_t existing_level = (it != existing.end()) ? it->level : 0;

        if (existing_level >= wanted_ench.level) {
            // Already have it at the same or higher level -- skip
            continue;
        }

        // Generate graduated books from existing_level+1 up to wanted_ench.level.
        // This gives the algorithm more intermediate options to find cheaper
        // sequences (e.g. existing=Sharpness III, wanted=Sharpness V will produce
        // books at IV and V rather than only V).
        for (int32_t lvl = existing_level + 1; lvl <= wanted_ench.level; ++lvl) {
            EnchSet book_enchs;
            book_enchs.emplace(wanted_ench.id, lvl);
            books.emplace_back(book_enchs, 0);
        }
    }
    return books;
}

// ===========================================================================
//  assemble_input
// ===========================================================================
ParsedInput InputParser::assemble_input(
    const CLIConfig &cli_config,
    const std::unordered_map<std::string, const Equipment*> &equipment_registry
) {
    // 1. Determine platform
    MCE platform;
    if (cli_config.platform == "auto") {
        platform = MCE::All;
    } else {
        platform = ParserUtils::parse_platform(cli_config.platform);
    }

    if (cli_config.mode == "direct") {
        // Parse target spec
        TargetSpec target_spec = CLIParser::parse_target(cli_config.target);
        ItemStack target = build_target(target_spec, equipment_registry);

        // Parse wanted enchantments
        std::vector<EnchantmentSpec> wanted_specs =
            CLIParser::parse_enchantment_list(cli_config.wanted);
        EnchSet wanted = build_wanted_enchset(wanted_specs);

        // Existing enchants come from the target item
        EnchSet existing = target.enchantments;

        // Auto-generate books for missing / upgrade enchantments
        ItemCollection books = generate_books(wanted, existing);
        // Stable sort by priority (lower = more preferred)
        std::stable_sort(books.begin(), books.end(),
            [](const ItemStack &a, const ItemStack &b) {
                return a.priority < b.priority;
            });

        return ParsedInput{platform, target.enchantments, target, books};
    }

    // ---- inventory mode ---------------------------------------------------
    if (!cli_config.input.has_value()) {
        throw std::runtime_error("Input file required for inventory mode");
    }

    ItemCollection available_items = parse_inventory(
        std::filesystem::path(cli_config.input.value()),
        equipment_registry
    );

    ItemStack target;
    if (!cli_config.target.empty()) {
        TargetSpec target_spec = CLIParser::parse_target(cli_config.target);
        target = build_target(target_spec, equipment_registry);
    }

    // Sort available items by priority (lower = more preferred)
    std::stable_sort(available_items.begin(), available_items.end(),
        [](const ItemStack &a, const ItemStack &b) {
            return a.priority < b.priority;
        });

    return ParsedInput{platform, target.enchantments, target, available_items};
}
