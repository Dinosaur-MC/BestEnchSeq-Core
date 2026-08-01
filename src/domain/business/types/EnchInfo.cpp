#include "EnchInfo.h"
#include "domain/business/components/Serializer.h"

Json EnchInfo::to_json() const {
    Json obj = Json::object()
        .set("id", id.str())
        .set("name", name)
        .set("supported_platform", std::string(Serializer::mce_to_string(supported_platform)))
        .set("max_level", max_level)
        .set("limited_level", limited_level)
        .set("multiplier", multiplier)
        .set("is_treasure", is_treasure);

    // exclusive_set -> array of NSID strings
    {
        Json arr = Json::array();
        for (const auto& excl : exclusive_set)
            arr.push_back(Json(excl.str()));
        obj.set("exclusive_set", std::move(arr));
    }

    // supported_items -> array of NSID strings
    {
        Json arr = Json::array();
        for (const auto& eq : supported_items)
            arr.push_back(Json(eq.str()));
        obj.set("supported_items", std::move(arr));
    }

    return obj;
}

void EnchInfo::from_json(const Json& json) {
    if (json.has("id"))
        id = NSID(json["id"].as<std::string>());
    if (json.has("name"))
        name = json["name"].as<std::string>();
    if (json.has("supported_platform"))
        supported_platform = Serializer::string_to_mce(json["supported_platform"].as<std::string>());
    if (json.has("max_level"))
        max_level = static_cast<int32_t>(json["max_level"].as<int64_t>());
    if (json.has("limited_level"))
        limited_level = static_cast<int32_t>(json["limited_level"].as<int64_t>());
    if (json.has("multiplier"))
        multiplier = static_cast<int32_t>(json["multiplier"].as<int64_t>());
    if (json.has("is_treasure"))
        is_treasure = json["is_treasure"].as<bool>();

    // exclusive_set
    if (json.has("exclusive_set")) {
        auto arr = json["exclusive_set"].as_array();
        for (const auto& elem : arr) {
            auto s = elem.as<std::string>();
            if (!s.empty()) exclusive_set.insert(NSID(std::move(s)));
        }
    }

    // supported_items
    if (json.has("supported_items")) {
        auto arr = json["supported_items"].as_array();
        for (const auto& elem : arr) {
            auto s = elem.as<std::string>();
            if (!s.empty()) supported_items.insert(NSID(std::move(s)));
        }
    }
}
