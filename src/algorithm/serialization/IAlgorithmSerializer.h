#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

class IAlgorithm;

class IAlgorithmSerializer {
public:
    virtual ~IAlgorithmSerializer() = default;
    virtual std::string_view tag() const noexcept = 0;
    virtual std::vector<uint8_t> serialize(const IAlgorithm& algo) const = 0;
    virtual void deserialize(IAlgorithm& algo, std::span<const uint8_t> data) const = 0;
};
