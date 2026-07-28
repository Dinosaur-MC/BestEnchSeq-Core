#include "Ench.h"

Json Ench::to_json() const {
    return Json::object()
        .set("id", id.str())
        .set("name", id.str())       // backward compat — name derived from NSID + i18n
        .set("level", level);
}

void Ench::from_json(const Json& json) {
    id = NSID(json["id"].as<std::string>());
    // name field is read for backward compat but no longer stored internally
    level = static_cast<int32_t>(json["level"].as<int64_t>());
}
