#include "algorithm/strategies/astar/AStarStateSerializer.h"
#include "algorithm/strategies/astar/AStarAlgorithm.h"

// ─── Public interface (placeholder) ─────────────────────────────────────────

std::vector<uint8_t> AStarStateSerializer::serialize(const IAlgorithm& algo) const {
    (void)algo;
    return {};
}

void AStarStateSerializer::deserialize(IAlgorithm& algo, std::span<const uint8_t> data) const {
    (void)algo;
    (void)data;
}

// ─── Serialization stubs (populated in Task 4) ──────────────────────────────

void AStarStateSerializer::_serialize_item_pool(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    (void)w;
    (void)astar;
}

void AStarStateSerializer::_serialize_step_pool(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    (void)w;
    (void)astar;
}

void AStarStateSerializer::_serialize_open_heap(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    (void)w;
    (void)astar;
}

void AStarStateSerializer::_serialize_best_g(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    (void)w;
    (void)astar;
}

void AStarStateSerializer::_serialize_scalars(ByteStreamWriter& w, const AStarAlgorithm& astar) const {
    (void)w;
    (void)astar;
}

// ─── Deserialization stubs (populated in Task 4) ────────────────────────────

void AStarStateSerializer::_deserialize_item_pool(ByteStreamReader& r, AStarAlgorithm& astar) const {
    (void)r;
    (void)astar;
}

void AStarStateSerializer::_deserialize_step_pool(ByteStreamReader& r, AStarAlgorithm& astar) const {
    (void)r;
    (void)astar;
}

void AStarStateSerializer::_deserialize_open_heap(ByteStreamReader& r, AStarAlgorithm& astar) const {
    (void)r;
    (void)astar;
}

void AStarStateSerializer::_deserialize_best_g(ByteStreamReader& r, AStarAlgorithm& astar) const {
    (void)r;
    (void)astar;
}

void AStarStateSerializer::_deserialize_scalars(ByteStreamReader& r, AStarAlgorithm& astar) const {
    (void)r;
    (void)astar;
}
