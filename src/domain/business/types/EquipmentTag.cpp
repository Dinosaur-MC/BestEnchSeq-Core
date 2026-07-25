#include "EquipmentTag.h"

Json EquipmentTag::to_json() const {
    return Json::object()
        .set("id", id.str())
        .set("name", name);
}

void EquipmentTag::from_json(const Json& json) {
    if (json.has("id"))
        id = NSID(json["id"].as<std::string>());
    if (json.has("name"))
        name = json["name"].as<std::string>();
}
