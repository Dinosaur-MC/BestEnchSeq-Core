#include "EquipmentTag.h"
#include "domain/business/schemas/EquipmentSchema.h"

Json EquipmentTag::to_json() const {
    return business::schema::EquipTagJsonSchema::serialize(*this);
}

void EquipmentTag::from_json(const Json& json) {
    business::schema::EquipTagJsonSchema::parse_or_throw(json, *this);
}
