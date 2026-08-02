#include "EnchInfo.h"
#include "domain/business/schemas/EnchInfoSchema.h"

Json EnchInfo::to_json() const {
    return business::schema::EnchJsonSchema::serialize(*this);
}

void EnchInfo::from_json(const Json& json) {
    business::schema::EnchJsonSchema::parse_or_throw(json, *this);
}
