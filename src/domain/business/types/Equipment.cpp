#include "Equipment.h"
#include "domain/business/schemas/EquipmentSchema.h"

Json Equipment::to_json() const {
    return business::schema::EquipJsonSchema::serialize(*this);
}

void Equipment::from_json(const Json& json) {
    business::schema::EquipJsonSchema::parse_or_throw(json, *this);
}
