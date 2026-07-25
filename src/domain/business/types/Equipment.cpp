#include "Equipment.h"

Json Equipment::to_json() const {
    return Json::object()
        .set("id", id.str())
        .set("name", name)
        .set("category", category.str())
        .set("max_durability", max_durability);
}

void Equipment::from_json(const Json& json) {
    if (json.has("id"))
        id = NSID(json["id"].as<std::string>());
    if (json.has("name"))
        name = json["name"].as<std::string>();
    if (json.has("category"))
        category = NSID(json["category"].as<std::string>());
    if (json.has("max_durability"))
        max_durability = static_cast<int32_t>(json["max_durability"].as<int64_t>());
}
