#pragma once
#include "ByteStream.h"
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
