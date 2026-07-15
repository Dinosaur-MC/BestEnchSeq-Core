#pragma once
#include "algorithm/serialization/IAlgorithmSerializer.h"
#include "io/ByteStream.h"
#include <cstdint>
#include <vector>

class AStarAlgorithm;

class AStarStateSerializer : public IAlgorithmSerializer {
public:
    std::string_view tag() const noexcept override { return "astar_v1"; }

    std::vector<uint8_t> serialize(const IAlgorithm& algo) const override;
    void deserialize(IAlgorithm& algo, std::span<const uint8_t> data) const override;

private:
    void _serialize_item_pool(ByteStreamWriter& w, const AStarAlgorithm& astar) const;
    void _serialize_step_pool(ByteStreamWriter& w, const AStarAlgorithm& astar) const;
    void _serialize_open_heap(ByteStreamWriter& w, const AStarAlgorithm& astar) const;
    void _serialize_best_g(ByteStreamWriter& w, const AStarAlgorithm& astar) const;
    void _serialize_scalars(ByteStreamWriter& w, const AStarAlgorithm& astar) const;

    void _deserialize_item_pool(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _deserialize_step_pool(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _deserialize_open_heap(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _deserialize_best_g(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _deserialize_scalars(ByteStreamReader& r, AStarAlgorithm& astar) const;
};
