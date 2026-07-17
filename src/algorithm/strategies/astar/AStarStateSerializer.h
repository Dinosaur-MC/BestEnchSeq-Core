#pragma once
#include "algorithm/serialization/IAlgorithmSerializer.h"
#include <cstdint>
#include <vector>

class AStarAlgorithm;

class AStarStateSerializer : public IAlgorithmSerializer {
public:
    std::string_view algorithm_name() const noexcept override { return "astar"; }
    std::string_view algorithm_version() const noexcept override { return "2.0.0"; }

protected:
    void _write_algo_sections(ByteStreamWriter& w,
                              const IAlgorithm& algo,
                              uint32_t& next_section_id) const override;

    void _read_algo_sections(ByteStreamReader& r,
                             IAlgorithm& algo) const override;

private:
    // Per-section serializers
    void _write_item_pool(ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const;
    void _write_step_pool(ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const;
    void _write_open_heap(ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const;
    void _write_best_g(ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const;
    void _write_scalars(ByteStreamWriter& w, const AStarAlgorithm& astar, uint32_t& sid) const;

    void _read_item_pool(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _read_step_pool(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _read_open_heap(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _read_best_g(ByteStreamReader& r, AStarAlgorithm& astar) const;
    void _read_scalars(ByteStreamReader& r, AStarAlgorithm& astar) const;
};
