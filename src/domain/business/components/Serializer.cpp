#include "Serializer.h"

#include <algorithm>
#include <cctype>

// ══════════════════════════════════════════════════════════════════════════
// Internal helpers — Json value extraction
// ══════════════════════════════════════════════════════════════════════════

namespace {

int32_t json_int32(const Json& j, int32_t def = 0) {
    if (j.type() != JsonType::Number) return def;
    auto num = std::get<Json::Number>(j.get_value());
    if (std::holds_alternative<int32_t>(num)) return std::get<int32_t>(num);
    if (std::holds_alternative<int64_t>(num)) return static_cast<int32_t>(std::get<int64_t>(num));
    return def;
}

int64_t json_int64(const Json& j, int64_t def = 0) {
    if (j.type() != JsonType::Number) return def;
    auto num = std::get<Json::Number>(j.get_value());
    if (std::holds_alternative<int32_t>(num)) return std::get<int32_t>(num);
    if (std::holds_alternative<int64_t>(num)) return std::get<int64_t>(num);
    return def;
}

std::string json_str(const Json& j) {
    if (j.type() != JsonType::String) return {};
    return std::get<Json::String>(j.get_value());
}

bool json_bool(const Json& j, bool def = false) {
    if (j.type() != JsonType::Bool) return def;
    return std::get<Json::Bool>(j.get_value());
}

Json::Array json_arr(const Json& j) {
    if (j.type() != JsonType::Array) return {};
    return std::get<Json::Array>(j.get_value());
}

Json::Object json_obj(const Json& j) {
    if (j.type() != JsonType::Object) return {};
    return std::get<Json::Object>(j.get_value());
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

/// Serialize an ItemCollection (vector<Item>) as a Json array.
Json item_collection_to_json(const ItemCollection& items) {
    Json::Array arr;
    arr.reserve(items.size());
    for (const auto& item : items) {
        Json j;
        j << item;
        arr.push_back(std::move(j));
    }
    return Json(arr);
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
    Json::Object obj;
    obj["id"]    = Json(Json::Number(ench.id));
    obj["level"] = Json(Json::Number(ench.level));
    json = Json(obj);
    return json;
}

const Json& operator>>(const Json& json, Ench& ench) {
    auto obj = json_obj(json);
    ench.id    = json_int32(json_get(obj, "id"));
    ench.level = json_int32(json_get(obj, "level"), 1);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EnchInfo
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EnchInfo& info) {
    Json::Object obj;
    obj["name_id"]             = Json(Json::String(info.name_id));
    obj["name"]                = Json(Json::String(info.name));
    obj["supported_platform"]  = Json(Json::String(std::string(
                                     Serializer::mce_to_string(info.supported_platform))));
    obj["max_level"]           = Json(Json::Number(info.max_level));
    obj["limited_level"]       = Json(Json::Number(info.limited_level));
    obj["multiplier"]          = Json(Json::Number(info.multiplier));
    obj["is_treasure"]         = Json(Json::Bool(info.is_treasure));

    // exclusive_set → array of strings
    {
        Json::Array arr;
        arr.reserve(info.exclusive_set.size());
        for (const auto& excl : info.exclusive_set)
            arr.push_back(Json(Json::String(excl)));
        obj["exclusive_set"] = Json(std::move(arr));
    }

    // applicable_category_ids → array of ints
    {
        Json::Array arr;
        arr.reserve(info.applicable_category_ids.size());
        for (int32_t cid : info.applicable_category_ids)
            arr.push_back(Json(Json::Number(cid)));
        obj["applicable_category_ids"] = Json(std::move(arr));
    }

    json = Json(obj);
    return json;
}

const Json& operator>>(const Json& json, EnchInfo& info) {
    auto obj = json_obj(json);
    info.name_id            = json_str(json_get(obj, "name_id"));
    info.name               = json_str(json_get(obj, "name"));
    info.supported_platform = Serializer::string_to_mce(json_str(json_get(obj, "supported_platform")));
    info.max_level          = json_int32(json_get(obj, "max_level"));
    info.limited_level      = json_int32(json_get(obj, "limited_level"));
    info.multiplier         = json_int32(json_get(obj, "multiplier"));
    info.is_treasure        = json_bool(json_get(obj, "is_treasure"));

    // exclusive_set
    {
        auto arr = json_arr(json_get(obj, "exclusive_set"));
        for (const auto& elem : arr) {
            auto s = json_str(elem);
            if (!s.empty()) info.exclusive_set.insert(std::move(s));
        }
    }

    // applicable_category_ids
    {
        auto arr = json_arr(json_get(obj, "applicable_category_ids"));
        for (const auto& elem : arr)
            info.applicable_category_ids.insert(json_int32(elem));
    }

    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EnchSet
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EnchSet& set) {
    Json::Array arr;
    arr.reserve(set.size());
    for (const auto& ench : set) {
        Json j;
        j << ench;
        arr.push_back(std::move(j));
    }
    json = Json(arr);
    return json;
}

const Json& operator>>(const Json& json, EnchSet& set) {
    set.clear();
    auto arr = json_arr(json);
    for (const auto& elem : arr) {
        Ench e;
        elem >> e;
        set.insert(std::move(e));
    }
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EquipmentCategory
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EquipmentCategory& cat) {
    Json::Object obj;
    obj["id"]      = Json(Json::Number(cat.id));
    obj["name_id"] = Json(Json::String(cat.name_id));
    json = Json(obj);
    return json;
}

const Json& operator>>(const Json& json, EquipmentCategory& cat) {
    auto obj = json_obj(json);
    cat.id      = json_int32(json_get(obj, "id"));
    cat.name_id = json_str(json_get(obj, "name_id"));
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Equipment
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Equipment& eq) {
    Json::Object obj;
    obj["name_id"]        = Json(Json::String(eq.name_id));
    obj["name"]           = Json(Json::String(eq.name));
    obj["category_id"]    = Json(Json::Number(eq.category_id));
    obj["max_durability"] = Json(Json::Number(eq.max_durability));
    json = Json(obj);
    return json;
}

const Json& operator>>(const Json& json, Equipment& eq) {
    auto obj = json_obj(json);
    eq.name_id        = json_str(json_get(obj, "name_id"));
    eq.name           = json_str(json_get(obj, "name"));
    eq.category_id    = json_int32(json_get(obj, "category_id"));
    eq.max_durability = json_int32(json_get(obj, "max_durability"));
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Item
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Item& item) {
    Json::Object obj;

    if (item.equipment.has_value()) {
        Json eq_j;
        eq_j << *item.equipment;
        obj["equipment"] = std::move(eq_j);
    } else {
        obj["equipment"] = Json::null();
    }

    {
        Json es_j;
        es_j << item.enchantments;
        obj["enchantments"] = std::move(es_j);
    }
    obj["prior_penalty"] = Json(Json::Number(item.prior_penalty));
    obj["durability"]    = Json(Json::Number(item.durability));
    obj["priority"]      = Json(Json::Number(item.priority));

    json = Json(obj);
    return json;
}

const Json& operator>>(const Json& json, Item& item) {
    auto obj = json_obj(json);

    auto eq_j = json_get(obj, "equipment");
    if (eq_j.type() == JsonType::Object) {
        Equipment eq;
        eq_j >> eq;
        item.equipment = std::move(eq);
    } else {
        item.equipment = std::nullopt;
    }

    json_get(obj, "enchantments") >> item.enchantments;
    item.prior_penalty = json_int32(json_get(obj, "prior_penalty"));
    item.durability    = json_int32(json_get(obj, "durability"));
    item.priority      = json_int32(json_get(obj, "priority"), 99);

    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Solution::EnchStep
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Solution::EnchStep& step) {
    Json::Object obj;
    {
        Json a, b;
        a << step.item_a;
        b << step.item_b;
        obj["item_a"]         = std::move(a);
        obj["item_b"]         = std::move(b);
    }
    obj["exp_level_cost"] = Json(Json::Number(step.exp_level_cost));
    obj["exp_cost"]       = Json(Json::Number(step.exp_cost));
    json = Json(obj);
    return json;
}

const Json& operator>>(const Json& json, Solution::EnchStep& step) {
    auto obj = json_obj(json);
    json_get(obj, "item_a") >> step.item_a;
    json_get(obj, "item_b") >> step.item_b;
    step.exp_level_cost = json_int32(json_get(obj, "exp_level_cost"));
    step.exp_cost       = json_int32(json_get(obj, "exp_cost"));
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Solution::MetaData
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Solution::MetaData& meta) {
    Json::Object obj;
    obj["algorithm_name"]    = Json(Json::String(meta.algorithm_name));
    obj["version"]           = Json(Json::String(meta.version));
    obj["created_at"]        = Json(Json::Number(static_cast<int64_t>(meta.created_at)));
    obj["computation_time"]  = Json(Json::Number(static_cast<int64_t>(meta.computation_time)));
    json = Json(obj);
    return json;
}

const Json& operator>>(const Json& json, Solution::MetaData& meta) {
    auto obj = json_obj(json);
    meta.algorithm_name   = json_str(json_get(obj, "algorithm_name"));
    meta.version          = json_str(json_get(obj, "version"));
    meta.created_at       = static_cast<size_t>(json_int64(json_get(obj, "created_at")));
    meta.computation_time = static_cast<size_t>(json_int64(json_get(obj, "computation_time")));
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// Solution
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const Solution& sol) {
    Json::Object obj;

    {
        Json mj;
        mj << sol.metadata;
        obj["metadata"] = std::move(mj);
    }

    obj["platform"]  = Json(Json::String(
                          std::string(Serializer::mce_to_string(sol.platform))));

    {
        Json oj, tj;
        oj << sol.original_ench;
        tj << sol.target_item;
        obj["original_ench"] = std::move(oj);
        obj["target_item"]   = std::move(tj);
    }

    obj["available_items"] = item_collection_to_json(sol.available_items);

    obj["total_exp_level_cost"] = Json(Json::Number(sol.total_exp_level_cost));
    obj["total_exp_cost"]       = Json(Json::Number(sol.total_exp_cost));

    // steps array
    {
        Json::Array arr;
        arr.reserve(sol.steps.size());
        for (const auto& step : sol.steps) {
            Json sj;
            sj << step;
            arr.push_back(std::move(sj));
        }
        obj["steps"] = Json(std::move(arr));
    }

    obj["max_cost_step_index"] = Json(Json::Number(static_cast<int64_t>(sol.max_cost_step_index)));
    obj["is_success"]          = Json(Json::Bool(sol.is_success));

    json = Json(obj);
    return json;
}

const Json& operator>>(const Json& json, Solution& sol) {
    auto obj = json_obj(json);

    json_get(obj, "metadata") >> sol.metadata;
    sol.platform              = Serializer::string_to_mce(json_str(json_get(obj, "platform")));
    json_get(obj, "original_ench") >> sol.original_ench;
    json_get(obj, "target_item") >> sol.target_item;
    sol.total_exp_level_cost  = json_int32(json_get(obj, "total_exp_level_cost"));
    sol.total_exp_cost        = json_int32(json_get(obj, "total_exp_cost"));

    // available_items
    {
        auto arr = json_arr(json_get(obj, "available_items"));
        sol.available_items.reserve(arr.size());
        for (const auto& elem : arr) {
            Item it;
            elem >> it;
            sol.available_items.push_back(std::move(it));
        }
    }

    // steps
    {
        auto arr = json_arr(json_get(obj, "steps"));
        sol.steps.reserve(arr.size());
        for (const auto& elem : arr) {
            Solution::EnchStep es;
            elem >> es;
            sol.steps.push_back(std::move(es));
        }
    }

    sol.max_cost_step_index = static_cast<size_t>(json_int64(json_get(obj, "max_cost_step_index")));
    sol.is_success          = json_bool(json_get(obj, "is_success"));

    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EnchantmentRegistry
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EnchantmentRegistry& reg) {
    Json::Array arr;
    const auto& instances = reg.get_instances();
    arr.reserve(instances.size());
    for (const auto& info : instances) {
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
        reg.initialize(infos);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EquipmentRegistry
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EquipmentRegistry& reg) {
    Json::Array arr;
    const auto& instances = reg.get_instances();
    arr.reserve(instances.size());
    for (const auto& eq : instances) {
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
        reg.initialize(eq_list);
    return json;
}

// ══════════════════════════════════════════════════════════════════════════
// EquipmentCategoryRegistry
// ══════════════════════════════════════════════════════════════════════════

Json& operator<<(Json& json, const EquipmentCategoryRegistry& reg) {
    Json::Array arr;
    const auto& instances = reg.get_instances();
    arr.reserve(instances.size());
    for (const auto& cat : instances) {
        Json j;
        j << cat;
        arr.push_back(std::move(j));
    }
    json = Json(arr);
    return json;
}

const Json& operator>>(const Json& json, EquipmentCategoryRegistry& reg) {
    auto arr = json_arr(json);
    std::vector<std::string> names;
    names.reserve(arr.size());
    for (const auto& elem : arr) {
        if (elem.type() == JsonType::String) {
            names.push_back(json_str(elem));
        } else {
            auto obj = json_obj(elem);
            names.push_back(json_str(json_get(obj, "name_id")));
        }
    }

    reg.initialize(names);
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
