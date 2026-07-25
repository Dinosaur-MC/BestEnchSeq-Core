#include "Serializer.h"

#include <algorithm>
#include <cctype>

// ══════════════════════════════════════════════════════════════════════════
// Internal helpers — Json value extraction
// ══════════════════════════════════════════════════════════════════════════

namespace {

int32_t json_int32(const Json& j, int32_t def = 0) {
    if (j.type() != JsonType::Number) return def;
    return static_cast<int32_t>(j.as_int());
}

int64_t json_int64(const Json& j, int64_t def = 0) {
    if (j.type() != JsonType::Number) return def;
    return j.as_int();
}

std::string json_str(const Json& j) {
    return j.as_string();
}

bool json_bool(const Json& j, bool def = false) {
    if (j.type() != JsonType::Bool) return def;
    return j.as_bool();
}

Json::Array json_arr(const Json& j) {
    return j.as_array();
}

Json::Object json_obj(const Json& j) {
    return j.as_object();
}

Json json_get(const Json::Object& obj, const std::string& key) {
    auto it = obj.find(key);
    if (it != obj.end()) return it->second;
    return Json::null();
}

bool iequals(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) ==
                                                   std::tolower(static_cast<unsigned char>(y)); });
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// MCE platform conversion
// ══════════════════════════════════════════════════════════════════════════

std::string_view Serializer::mce_to_string(MCE platform) noexcept {
    switch (platform) {
        case MCE::None:    return "none";
        case MCE::Java:    return "java";
        case MCE::Bedrock: return "bedrock";
        case MCE::All:     return "all";
    }
    return "none";
}

MCE Serializer::string_to_mce(std::string_view str) noexcept {
    if (iequals(str, "java"))    return MCE::Java;
    if (iequals(str, "bedrock")) return MCE::Bedrock;
    if (iequals(str, "all"))     return MCE::All;
    return MCE::None;
}

// ══════════════════════════════════════════════════════════════════════════
// Ench
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Ench& ench) {
    json = ench.to_json();
    return json;
}

const Json& operator>>(const Json& json, Ench& ench) {
    ench.from_json(json);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EnchInfo
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EnchInfo& info) {
    json = info.to_json();
    return json;
}

const Json& operator>>(const Json& json, EnchInfo& info) {
    info.from_json(json);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EnchSet
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EnchSet& set) {
    json = set.to_json();
    return json;
}

const Json& operator>>(const Json& json, EnchSet& set) {
    set.from_json(json);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EquipmentTag
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EquipmentTag& tag) {
    json = tag.to_json();
    return json;
}

const Json& operator>>(const Json& json, EquipmentTag& tag) {
    tag.from_json(json);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Equipment
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Equipment& eq) {
    json = eq.to_json();
    return json;
}

const Json& operator>>(const Json& json, Equipment& eq) {
    eq.from_json(json);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Item
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Item& item) {
    json = item.to_json();
    return json;
}

const Json& operator>>(const Json& json, Item& item) {
    item.from_json(json);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Solution::EnchStep
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Solution::EnchStep& step) {
    json = step.to_json();
    return json;
}

const Json& operator>>(const Json& json, Solution::EnchStep& step) {
    step.from_json(json);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Solution::MetaData
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Solution::MetaData& meta) {
    json = meta.to_json();
    return json;
}

const Json& operator>>(const Json& json, Solution::MetaData& meta) {
    meta.from_json(json);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Solution
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Solution& sol) {
    json = sol.to_json();
    return json;
}

const Json& operator>>(const Json& json, Solution& sol) {
    sol.from_json(json);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EnchantmentRegistry
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EnchantmentRegistry& reg) {
    Json::Array arr;
    arr.reserve(reg.size());
    for (const auto& [nsid, info] : reg.data()) {
        Json j;
        j << info;
        arr.push_back(std::move(j));
    }
    json = Json(arr);
    return json;
}

const Json& operator>>(const Json& json, EnchantmentRegistry& reg) {
    auto arr = json_arr(json);
    std::vector<EnchInfo> infos;
    infos.reserve(arr.size());
    for (const auto& elem : arr) {
        EnchInfo info;
        elem >> info;
        infos.push_back(std::move(info));
    }

    if (!infos.empty())
        reg = EnchantmentRegistry(infos);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EquipmentRegistry
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EquipmentRegistry& reg) {
    Json::Array arr;
    arr.reserve(reg.size());
    for (const auto& [id, eq] : reg.data()) {
        Json j;
        j << eq;
        arr.push_back(std::move(j));
    }
    json = Json(arr);
    return json;
}

const Json& operator>>(const Json& json, EquipmentRegistry& reg) {
    auto arr = json_arr(json);
    std::vector<Equipment> eq_list;
    eq_list.reserve(arr.size());
    for (const auto& elem : arr) {
        Equipment eq;
        elem >> eq;
        eq_list.push_back(std::move(eq));
    }

    if (!eq_list.empty())
        reg = EquipmentRegistry(eq_list);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EquipmentTagRegistry
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EquipmentTagRegistry& reg) {
    Json::Array arr;
    arr.reserve(reg.size());
    for (const auto& [id, tag] : reg.data()) {
        Json j;
        j << tag;
        arr.push_back(std::move(j));
    }
    json = Json(arr);
    return json;
}

const Json& operator>>(const Json& json, EquipmentTagRegistry& reg) {
    auto arr = json_arr(json);
    std::vector<std::string> names;
    names.reserve(arr.size());
    for (const auto& elem : arr) {
        if (elem.type() == JsonType::String) {
            names.push_back(json_str(elem));
        } else {
            auto obj = json_obj(elem);
            names.push_back(json_str(json_get(obj, "name")));
        }
    }

    std::vector<EquipmentTag> tags;
    tags.reserve(names.size());
    for (const auto& name : names) {
        EquipmentTag tag;
        tag.id = NSID("#minecraft:" + name);
        tag.name = name;
        tags.push_back(std::move(tag));
    }
    reg = EquipmentTagRegistry(std::move(tags));
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Convenience
// ══════════════════════════════════════════════════════════════════════════

std::string Serializer::to_string(const Json& json, Json::JsonStyle style) {
    return json.to_string(style);
}

Json Serializer::from_string(const std::string& str) {
    return Json::parse(str);
}
