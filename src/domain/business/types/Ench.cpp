#include "Ench.h"

Json Ench::to_json() const {
    return Json::object()
        .set("id", id.str())
        .set("name", name)
        .set("level", level);
}

void Ench::from_json(const Json& json) {
    id = NSID(json["id"].as<std::string>());
    name = json["name"].as<std::string>();
    level = static_cast<int32_t>(json["level"].as<int64_t>());
}
