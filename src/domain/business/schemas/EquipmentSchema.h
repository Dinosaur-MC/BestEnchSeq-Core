#pragma once
#include "domain/business/schemas/Converters.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/dto/EquipmentData.h"
#include "ds/ds.h"

#include <tuple>

namespace business::schema {

struct EquipmentSchema {
    using Type = Equipment;
    static constexpr auto fields = std::tuple{
        ds::field("id",              &Type::id,             ds::text_codec<NSIDConverter>{}),
        ds::field("name",            &Type::name,           ds::string_codec{}),
        ds::field("category",        &Type::category,       ds::text_codec<NSIDConverter>{}),
        ds::field("max_durability",  &Type::max_durability, ds::int_codec{}),
    };
};

struct EquipmentDataSchema {   // DTO（伴生文件解析用）
    using Type = business::loader::EquipmentData;
    static constexpr auto fields = std::tuple{
        ds::field("id",              &Type::id,             ds::string_codec{}),
        ds::field("name",            &Type::display_name,   ds::string_codec{}),
        ds::field("category",        &Type::category,       ds::string_codec{}),
        ds::field("max_durability",  &Type::max_durability, ds::int_codec{}),
    };
};

struct EquipmentTagSchema {
    using Type = EquipmentTag;
    static constexpr auto fields = std::tuple{
        ds::field("id",   &Type::id,   ds::text_codec<NSIDConverter>{}),
        ds::field("name", &Type::name, ds::string_codec{}),
    };
};

// ── JSON binder 别名（使用方免去手动拼 ds::json::Schema<S>）────────────
using EquipJsonSchema    = ds::json::Schema<EquipmentSchema>;
using EquipmentDataJson  = ds::json::Schema<EquipmentDataSchema>;
using EquipTagJsonSchema = ds::json::Schema<EquipmentTagSchema>;

// ── CSV binder 别名 ────────────────────────────────────────────────────
using EquipCsv           = ds::csv::Schema<EquipmentSchema>;
using EquipmentDataCsv   = ds::csv::Schema<EquipmentDataSchema>;

} // namespace business::schema
