#pragma once
#include "domain/interface/cli/InventorySchema.h"
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

// ============================================================================
// WebSchema — the GUI task schema = InvTaskSchema + optional direct-mode
// `source` + optional search-config fields.
//
// All InvTaskSchema payloads (profile/target/items/algorithm) parse unchanged.
// Extra fields are optional and never change existing semantics, so a task
// saved by the GUI loads straight into `besq --input`.
// ============================================================================

struct WebTaskDto {
    InvTargetDto target;
    std::vector<InvItemDto> items;
    std::string algorithm; // empty = not specified
    std::string profile;   // empty = not specified

    // ── Direct-mode source (extended) ──
    std::vector<InvEnchDto> source; // [{id,level}] — the item's current enchants

    // ── Search config (extended, optional) ──
    int32_t max_solutions = 0;      // 0 = strategy default
    int64_t max_search_time_ms = 0; // 0 = SearchConfig default (180s)
    uint32_t max_threads = 0;       // 0 = hardware concurrency

    /// 允许不兼容（batch C）：转发到 ForgeConfig.ignore_imcompatible（内部拼写
    /// 保留），wire 键用正确的 incompatible 拼写。false = 严格冲突（默认）。
    bool ignore_incompatible = false;

    /// 忽略附魔惩罚成本（ForgeConfig.ignore_penalty_cost）。
    bool ignore_penalty_cost = false;
    /// 忽略装备+装备修理附加费（ForgeConfig.ignore_repair_cost）。
    bool ignore_repair_cost = false;
};

struct WebTaskSchema {
    using Type = WebTaskDto;
    static constexpr auto fields = std::tuple{
        ds::required_field("target", &Type::target, ds::object_codec<InvTargetSchema>{}),
        ds::field("items", &Type::items, ds::vector_codec<ds::object_codec<InvItemSchema>>{}),
        ds::field("algorithm", &Type::algorithm, ds::string_codec{}),
        ds::field("profile", &Type::profile, ds::string_codec{}),
        ds::field("source", &Type::source, ds::vector_codec<ds::object_codec<InvEnchSchema>>{}),
        ds::field("max_solutions", &Type::max_solutions, ds::int_codec{}),
        ds::field(
            "max_search_time", &Type::max_search_time_ms, ds::int_codec{}), // wire key per spec §4.2; _ms is C++-side unit doc
        ds::field("max_threads", &Type::max_threads, ds::int_codec{}),
        ds::field("ignore_incompatible", &Type::ignore_incompatible, ds::bool_codec{}),
        ds::field("ignore_penalty_cost", &Type::ignore_penalty_cost, ds::bool_codec{}),
        ds::field("ignore_repair_cost", &Type::ignore_repair_cost, ds::bool_codec{}),
    };
};

using WebTaskJson = ds::json::Schema<WebTaskSchema>;