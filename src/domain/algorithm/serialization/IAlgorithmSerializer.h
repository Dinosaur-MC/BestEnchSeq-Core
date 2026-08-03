#pragma once
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/serialization/Checkpoint.h"
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace algorithm {
class IAlgorithm;

class IAlgorithmSerializer {
public:
    virtual ~IAlgorithmSerializer() = default;

    virtual std::string_view algorithm_name() const noexcept = 0;
    virtual std::string_view algorithm_version() const noexcept = 0;

    std::vector<uint8_t> serialize(const IAlgorithm& algo, const AlgorithmInput& input) const;
    bool deserialize(IAlgorithm& algo, AlgorithmInput& out_input, std::span<const uint8_t> data) const;

    /// Public access to JUST the algorithm-state sections (no checkpoint
    /// wrapping).  Used by the sandbox worker's IPC layer, which must produce
    /// / consume the state sections on behalf of the parent's proxy serializer.
    std::vector<checkpoint::Section> serialize_state(const IAlgorithm& algo) const {
        return _serialize_state(algo);
    }
    bool deserialize_state(IAlgorithm& algo, std::span<const checkpoint::Section> sections) const {
        return _deserialize_state(algo, sections);
    }

protected:
    virtual std::vector<checkpoint::Section> _serialize_state(const IAlgorithm& algo) const = 0;
    virtual bool _deserialize_state(IAlgorithm& algo, std::span<const checkpoint::Section> sections) const = 0;
};

} // namespace algorithm
