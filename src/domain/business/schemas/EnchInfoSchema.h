#pragma once
#include "domain/business/schemas/Converters.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/dto/EnchantmentData.h"
#include "ds/ds.h"

#include <tuple>

namespace business::schema {

/// 领域 schema：EnchInfo（NSID 已解析）。用于 EnchInfo::to_json/from_json 与
/// EnchSerializer 导出。键名 canonical = "platform"，读兼容旧 "supported_platform"。
struct EnchInfoSchema {
    using Type = EnchInfo;
    static constexpr auto fields = std::tuple{
        ds::field("id",             &Type::id,                  ds::text_codec<NSIDConverter>{}),
        ds::field("name",           &Type::name,                ds::string_codec{}),
        ds::field("platform",       &Type::supported_platform,  ds::text_codec<PlatformConv>{}, "supported_platform"),
        ds::field("max_level",      &Type::max_level,           ds::int_codec{}),
        ds::field("limited_level",  &Type::limited_level,       ds::int_codec{},
                  [](const Type& t) { return t.limited_level_provided; },
                  ds::presence_flag<&Type::limited_level_provided>{}),
        ds::field("min_cost_base",          &Type::min_cost_base,          ds::int_codec{},
                  [](const Type& t) { return t.min_cost_base != 0; }),
        ds::field("min_cost_per_level",     &Type::min_cost_per_level,     ds::int_codec{},
                  [](const Type& t) { return t.min_cost_per_level != 0; }),
        ds::field("multiplier",     &Type::multiplier,          ds::int_codec{}),
        ds::field("is_treasure",    &Type::is_treasure,         ds::bool_codec{}),
        ds::field("exclusive_set",  &Type::exclusive_set,       ds::set_codec<ds::text_codec<NSIDConverter>>{}),
        ds::field("supported_items",&Type::supported_items,     ds::set_codec<ds::text_codec<NSIDConverter>>{}),
    };
};

/// DTO schema：EnchantmentData（string 原始引用，两阶段交叉验证边界）。
/// min_cost 双形态（扁平主键 + 嵌套 alias min_cost.base）。
struct EnchantmentDataSchema {
    using Type = business::loader::EnchantmentData;
    static constexpr auto fields = std::tuple{
        ds::field("id",             &Type::id,               ds::string_codec{}),
        ds::field("name",           &Type::display_name,     ds::string_codec{}),
        ds::field("platform",       &Type::platform,         ds::string_codec{}, "supported_platform"),
        ds::field("max_level",      &Type::max_level,        ds::int_codec{}),
        ds::field("limited_level",  &Type::limited_level,    ds::int_codec{},
                  ds::presence_flag<&Type::limited_level_provided>{}),
        ds::field("min_cost_base",       &Type::min_cost_base,      ds::int_codec{}, "min_cost.base"),
        ds::field("min_cost_per_level",  &Type::min_cost_per_level, ds::int_codec{}, "min_cost.per_level_above_first"),
        ds::field("multiplier",     &Type::multiplier,       ds::int_codec{}),
        ds::field("is_treasure",    &Type::is_treasure,      ds::bool_codec{}),
        ds::field("exclusive_set",  &Type::exclusive_with,   ds::vector_codec<ds::string_codec>{}),
        ds::field("supported_items",&Type::applicable_to,    ds::vector_codec<ds::string_codec>{}),
    };
};

// ── JSON binder 别名（使用方免去手动拼 ds::json::Schema<S>）────────────
using EnchJsonSchema      = ds::json::Schema<EnchInfoSchema>;
using EnchantmentDataJson = ds::json::Schema<EnchantmentDataSchema>;

} // namespace business::schema
