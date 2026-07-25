#pragma once
#include "common/io/json.h"
#include <memory>
#include <vector>

// ══════════════════════════════════════════════════════════════════════════
// IJsonSerializable — polymorphic JSON serialization interface
//
// Types implement to_json() and from_json() to support generic serialization
// via the JsonSerializable concept and serialize()/deserialize() helpers.
//
// Usage:
//   struct MyData : IJsonSerializable {
//       Json to_json() const override { ... }
//       void from_json(const Json& json) override { ... }
//   };
//
//   MyData obj;
//   Json j = serialize(obj);            // → obj.to_json()
//   deserialize(obj, j);                // → obj.from_json(j)
//   auto obj2 = deserialize<MyData>(j); // → construct + from_json
//
// Backward compatible — existing operator<< / operator>> still work.
// ══════════════════════════════════════════════════════════════════════════

struct IJsonSerializable {
    virtual ~IJsonSerializable() = default;
    virtual Json to_json() const = 0;
    virtual void from_json(const Json& json) = 0;
};

// ── Concept ─────────────────────────────────────────────────────────────

template<typename T>
concept JsonSerializable = std::is_base_of_v<IJsonSerializable, T>;

// ── Template serialization helpers ──────────────────────────────────────

/// Serialize any JsonSerializable type to Json.
template<JsonSerializable T>
Json serialize(const T& obj) {
    return obj.to_json();
}

/// Deserialize from Json into an existing object.
template<JsonSerializable T>
void deserialize(T& obj, const Json& json) {
    obj.from_json(json);
}

/// Deserialize from Json into a new object.
template<JsonSerializable T>
T deserialize(const Json& json) {
    T obj;
    obj.from_json(json);
    return obj;
}

/// Serialize a vector of JsonSerializable pointers.
template<JsonSerializable T>
Json serialize_vector(const std::vector<std::unique_ptr<T>>& vec) {
    Json::Array arr;
    arr.reserve(vec.size());
    for (const auto& ptr : vec)
        arr.push_back(ptr->to_json());
    return Json(arr);
}

/// Serialize a vector of JsonSerializable values.
template<JsonSerializable T>
Json serialize_vector(const std::vector<T>& vec) {
    Json::Array arr;
    arr.reserve(vec.size());
    for (const auto& item : vec)
        arr.push_back(item.to_json());
    return Json(arr);
}
