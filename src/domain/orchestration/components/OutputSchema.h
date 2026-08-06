#pragma once
#include "common/ds/ds.h"
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

// ============================================================================
// Output schema layer — ds schema declarations for the machine-readable JSON
// output of OutputFormatter (--format json / export / web / C ABI root).
//
// The wire shape is declared ONCE here as ds schemas and the output JSON is
// assembled by `ds::json::Schema<S>::serialize` instead of hand-built Json
// objects.  Item serialization that needs registry context (equipment lookup,
// category / display-name resolution) happens while filling the *View structs
// (see OutputFormatter::make_*_view), whose members are plain wire values;
// the schemas themselves stay registry-free, like the inventory/web schemas.
//
// Root schema (v1.1):
//   { "schema_version": "1.1", "mode": ..., "success": ..., "algorithm": ...,
//     "computation_time_ms": ..., "solutions": [ ... ] }
// All fields are required (the output always emits them), so parsing the
// emitted JSON with these schemas doubles as a structural output check.
// ============================================================================

/// Wire schema version of the JSON output root (`schema_version` field).
inline constexpr const char* kOutputSchemaVersion = "1.1";

/// One enchantment entry: { "id", "level" }.
struct EnchView {
    std::string id;
    int32_t level = 0;
};
struct EnchSchema {
    using Type = EnchView;
    static constexpr auto fields = std::tuple{
        ds::required_field("id", &Type::id, ds::string_codec{}),
        ds::required_field("level", &Type::level, ds::int_codec{}),
    };
};

/// Equipment definition (present only for non-book items).
struct EquipmentView {
    std::string id;
    std::string category;
    std::string name;
    int32_t max_durability = 0;
};
struct EquipmentSchema {
    using Type = EquipmentView;
    static constexpr auto fields = std::tuple{
        ds::required_field("id", &Type::id, ds::string_codec{}),
        ds::required_field("category", &Type::category, ds::string_codec{}),
        ds::required_field("name", &Type::name, ds::string_codec{}),
        ds::required_field("max_durability", &Type::max_durability, ds::int_codec{}),
    };
};

/// Item stack: `equipment` is null for books (optional encodes empty as null).
struct ItemView {
    std::optional<EquipmentView> equipment;  // nullopt = book
    bool is_book = false;
    std::vector<EnchView> enchantments;
    int32_t prior_penalty = 0;
    int32_t durability = 0;
};
struct ItemSchema {
    using Type = ItemView;
    static constexpr auto fields = std::tuple{
        ds::required_field(
            "equipment", &Type::equipment,
            ds::optional_codec<ds::object_codec<EquipmentSchema>>{}),
        ds::required_field("is_book", &Type::is_book, ds::bool_codec{}),
        ds::required_field(
            "enchantments", &Type::enchantments,
            ds::vector_codec<ds::object_codec<EnchSchema>>{}),
        ds::required_field("prior_penalty", &Type::prior_penalty, ds::int_codec{}),
        ds::required_field("durability", &Type::durability, ds::int_codec{}),
    };
};

/// One forge operation: item_a + item_b → result.
struct StepView {
    ItemView item_a;
    ItemView item_b;
    ItemView result;
    int32_t exp_level_cost = 0;
    int32_t exp_cost = 0;
};
struct StepSchema {
    using Type = StepView;
    static constexpr auto fields = std::tuple{
        ds::required_field("item_a", &Type::item_a, ds::object_codec<ItemSchema>{}),
        ds::required_field("item_b", &Type::item_b, ds::object_codec<ItemSchema>{}),
        ds::required_field("result", &Type::result, ds::object_codec<ItemSchema>{}),
        ds::required_field("exp_level_cost", &Type::exp_level_cost, ds::int_codec{}),
        ds::required_field("exp_cost", &Type::exp_cost, ds::int_codec{}),
    };
};

/// Execution metadata of one solution.
struct MetadataView {
    std::string algorithm_name;
    std::string algorithm_version;
    int64_t created_at = 0;        // raw time_since_epoch().count() (typically ns)
    int64_t computation_time = 0;  // ms
};
struct MetadataSchema {
    using Type = MetadataView;
    static constexpr auto fields = std::tuple{
        ds::required_field("algorithm_name", &Type::algorithm_name, ds::string_codec{}),
        ds::required_field("algorithm_version", &Type::algorithm_version, ds::string_codec{}),
        ds::required_field("created_at", &Type::created_at, ds::int_codec{}),
        ds::required_field("computation_time", &Type::computation_time, ds::int_codec{}),
    };
};

/// One forge solution.
struct SolutionView {
    int32_t rank = 0;
    std::string platform;  // raw: "Java" / "Bedrock" / "All" / "None"
    std::vector<EnchView> original_ench;
    ItemView target_item;
    std::vector<ItemView> available_items;
    std::vector<StepView> steps;
    int32_t total_exp_level_cost = 0;
    int32_t total_exp_cost = 0;
    int32_t peak_level_cost = 0;
    int32_t peak_exp_cost = 0;
    int64_t max_cost_step_index = 0;
    bool is_success = false;
    MetadataView metadata;
};
struct SolutionSchema {
    using Type = SolutionView;
    static constexpr auto fields = std::tuple{
        ds::required_field("rank", &Type::rank, ds::int_codec{}),
        ds::required_field("platform", &Type::platform, ds::string_codec{}),
        ds::required_field(
            "original_ench", &Type::original_ench,
            ds::vector_codec<ds::object_codec<EnchSchema>>{}),
        ds::required_field("target_item", &Type::target_item,
                           ds::object_codec<ItemSchema>{}),
        ds::required_field(
            "available_items", &Type::available_items,
            ds::vector_codec<ds::object_codec<ItemSchema>>{}),
        ds::required_field("steps", &Type::steps,
                           ds::vector_codec<ds::object_codec<StepSchema>>{}),
        ds::required_field("total_exp_level_cost", &Type::total_exp_level_cost,
                           ds::int_codec{}),
        ds::required_field("total_exp_cost", &Type::total_exp_cost, ds::int_codec{}),
        ds::required_field("peak_level_cost", &Type::peak_level_cost, ds::int_codec{}),
        ds::required_field("peak_exp_cost", &Type::peak_exp_cost, ds::int_codec{}),
        ds::required_field("max_cost_step_index", &Type::max_cost_step_index,
                           ds::int_codec{}),
        ds::required_field("is_success", &Type::is_success, ds::bool_codec{}),
        ds::required_field("metadata", &Type::metadata,
                           ds::object_codec<MetadataSchema>{}),
    };
};

/// Root metadata object — shared by OutputFormatter::format_json and the C ABI
/// besq_solve (both assemble from these field declarations so they cannot
/// drift).
struct RootMetaView {
    std::string schema_version;
    std::string mode;
    bool success = false;
    std::string algorithm;
    int64_t computation_time_ms = 0;
};
struct RootMetaSchema {
    using Type = RootMetaView;
    static constexpr auto fields = std::tuple{
        ds::required_field("schema_version", &Type::schema_version, ds::string_codec{}),
        ds::required_field("mode", &Type::mode, ds::string_codec{}),
        ds::required_field("success", &Type::success, ds::bool_codec{}),
        ds::required_field("algorithm", &Type::algorithm, ds::string_codec{}),
        ds::required_field("computation_time_ms", &Type::computation_time_ms,
                           ds::int_codec{}),
    };
};

/// Full root object emitted by OutputFormatter::format_json.
struct RootView {
    std::string schema_version;
    std::string mode;
    bool success = false;
    std::string algorithm;
    int64_t computation_time_ms = 0;
    std::vector<SolutionView> solutions;
};
struct RootSchema {
    using Type = RootView;
    static constexpr auto fields = std::tuple{
        ds::required_field("schema_version", &Type::schema_version, ds::string_codec{}),
        ds::required_field("mode", &Type::mode, ds::string_codec{}),
        ds::required_field("success", &Type::success, ds::bool_codec{}),
        ds::required_field("algorithm", &Type::algorithm, ds::string_codec{}),
        ds::required_field("computation_time_ms", &Type::computation_time_ms,
                           ds::int_codec{}),
        ds::required_field("solutions", &Type::solutions,
                           ds::vector_codec<ds::object_codec<SolutionSchema>>{}),
    };
};
