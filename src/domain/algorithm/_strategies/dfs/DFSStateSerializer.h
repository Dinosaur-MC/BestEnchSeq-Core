#pragma once
#include "domain/algorithm/serialization/IAlgorithmSerializer.h"
#include <vector>

namespace algorithm {
class DFSAlgorithm;

class DFSStateSerializer : public IAlgorithmSerializer {
public:
    std::string_view algorithm_name() const noexcept override { return "dfs"; }
    std::string_view algorithm_version() const noexcept override { return "1.0.0"; }

protected:
    std::vector<checkpoint::Section> _serialize_state(const IAlgorithm& algo) const override;
    bool _deserialize_state(IAlgorithm& algo, std::span<const checkpoint::Section> sections) const override;

private:
    static checkpoint::Section _write_stack(const DFSAlgorithm& dfs);
    static checkpoint::Section _write_frame_pairs(const DFSAlgorithm& dfs);
    static checkpoint::Section _write_visited_best(const DFSAlgorithm& dfs);
    static checkpoint::Section _write_best_steps(const DFSAlgorithm& dfs);
    static checkpoint::Section _write_current_steps(const DFSAlgorithm& dfs);
    static checkpoint::Section _write_scalars(const DFSAlgorithm& dfs);

    void _read_stack(ByteStreamReader& r, DFSAlgorithm& dfs) const;
    void _read_frame_pairs(ByteStreamReader& r, DFSAlgorithm& dfs) const;
    void _read_visited_best(ByteStreamReader& r, DFSAlgorithm& dfs) const;
    void _read_best_steps(ByteStreamReader& r, DFSAlgorithm& dfs) const;
    void _read_current_steps(ByteStreamReader& r, DFSAlgorithm& dfs) const;
    void _read_scalars(ByteStreamReader& r, DFSAlgorithm& dfs) const;
};
} // namespace algorithm
