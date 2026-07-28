#pragma once
#include "domain/algorithm/serialization/IAlgorithmSerializer.h"
#include <vector>

namespace algorithm {
class DPMergeAlgorithm;

class DPMergeStateSerializer : public IAlgorithmSerializer {
public:
    std::string_view algorithm_name() const noexcept override { return "dp_merge"; }
    std::string_view algorithm_version() const noexcept override { return "1.0.0"; }

protected:
    std::vector<checkpoint::Section> _serialize_state(const IAlgorithm& algo) const override;
    bool _deserialize_state(IAlgorithm& algo, std::span<const checkpoint::Section> sections) const override;

private:
    static checkpoint::Section _write_cache(const DPMergeAlgorithm& dp);
    static checkpoint::Section _write_scalars(const DPMergeAlgorithm& dp);

    void _read_cache(ByteStreamReader& r, DPMergeAlgorithm& dp) const;
    void _read_scalars(ByteStreamReader& r, DPMergeAlgorithm& dp) const;
};

} // namespace algorithm
