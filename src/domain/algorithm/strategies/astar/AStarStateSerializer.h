#pragma once
#include "../../serialization/IAlgorithmSerializer.h"
#include <cstdint>
#include <vector>

namespace algorithm {
class AStarAlgorithm;

class AStarStateSerializer : public IAlgorithmSerializer {
public:
    std::string_view algorithm_name() const noexcept override { return "astar"; }
    std::string_view algorithm_version() const noexcept override { return "2.0.0"; }

protected:
    std::vector<compact_serial::AlgoSectionData> _serialize_state(const IAlgorithm& algo) const override;
    bool _deserialize_state(IAlgorithm& algo, std::span<const compact_serial::AlgoSectionData> sections) const override;

private:
    // Per-section helpers (all produce AlgoSectionData with logical tags)
    static compact_serial::AlgoSectionData _write_item_pool(const AStarAlgorithm& astar);
    static compact_serial::AlgoSectionData _write_step_pool(const AStarAlgorithm& astar);
    static compact_serial::AlgoSectionData _write_open_heap(const AStarAlgorithm& astar);
    static compact_serial::AlgoSectionData _write_best_g(const AStarAlgorithm& astar);
    static compact_serial::AlgoSectionData _write_scalars(const AStarAlgorithm& astar);

    void _read_item_pool(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _read_step_pool(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _read_open_heap(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _read_best_g(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _read_scalars(ByteStreamReader& r, AStarAlgorithm& astar) const;
};
} // namespace algorithm
