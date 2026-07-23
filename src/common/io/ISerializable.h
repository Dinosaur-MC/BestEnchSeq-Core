#pragma once
#include "ByteStream.h"
#include <concepts>
#include <vector>

struct ISerializable {
    virtual ~ISerializable() = default;

    virtual void serialize(ByteStreamWriter& w) const noexcept = 0;
    virtual void deserialize(ByteStreamReader& r) noexcept = 0;

    [[nodiscard]] std::vector<uint8_t> to_bytes() const {
        ByteStreamWriter w;
        serialize(w);
        return std::move(w).take();
    }

    [[nodiscard]] bool from_bytes(const std::vector<uint8_t>& data) noexcept {
        ByteStreamReader r(data);
        deserialize(r);
        return r.ok();
    }

    [[nodiscard]] bool from_bytes(const uint8_t* data, size_t size) noexcept {
        ByteStreamReader r(data, size);
        deserialize(r);
        return r.ok();
    }
};

// ── Free-function streaming operators for ISerializable ──

inline ByteStreamWriter& operator<<(ByteStreamWriter& w, const ISerializable& obj) {
    obj.serialize(w);
    return w;
}

inline ByteStreamReader& operator>>(ByteStreamReader& r, ISerializable& obj) {
    obj.deserialize(r);
    return r;
}

// ── vector<T> constrained to ISerializable subtypes ──

template <std::derived_from<ISerializable> T>
ByteStreamWriter& operator<<(ByteStreamWriter& w, const std::vector<T>& vec) {
    w << vec.size();
    for (const auto& v : vec) w << v;
    return w;
}

template <std::derived_from<ISerializable> T>
ByteStreamReader& operator>>(ByteStreamReader& r, std::vector<T>& vec) {
    size_t n;
    r.read(n);
    vec.resize(n);
    for (auto& v : vec) r >> v;
    return r;
}
