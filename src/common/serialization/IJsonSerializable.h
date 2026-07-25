#pragma once
#include "common/serialization/ISerializable.h"
#include "common/io/json.h"
#include <memory>
#include <type_traits>
#include <vector>

/// JSON serialization interface — inherits from ISerializable.
struct IJsonSerializable : ISerializable {
    virtual ~IJsonSerializable() = default;
    virtual Json to_json() const = 0;
    virtual void from_json(const Json& json) = 0;
};

// ── Concept ─────────────────────────────────────────────────────────────

template<typename T>
concept JsonSerializable = std::is_base_of_v<IJsonSerializable, T>;

// ── Template serialization helpers ──────────────────────────────────────

template<JsonSerializable T>
Json serialize(const T& obj) { return obj.to_json(); }

template<JsonSerializable T>
void deserialize(T& obj, const Json& json) { obj.from_json(json); }

template<JsonSerializable T>
T deserialize(const Json& json) {
    T obj;
    obj.from_json(json);
    return obj;
}

template<JsonSerializable T>
Json serialize_vector(const std::vector<std::unique_ptr<T>>& vec) {
    Json::Array arr;
    arr.reserve(vec.size());
    for (const auto& ptr : vec) arr.push_back(ptr->to_json());
    return Json(arr);
}

template<JsonSerializable T>
Json serialize_vector(const std::vector<T>& vec) {
    Json::Array arr;
    arr.reserve(vec.size());
    for (const auto& item : vec) arr.push_back(item.to_json());
    return Json(arr);
}
