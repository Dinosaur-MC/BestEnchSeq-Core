#include "IExecutor.h"
#include "common/io/ByteStream.h"

namespace algorithm {

checkpoint::MetaHeader IExecutor::peek(std::span<const uint8_t> data) {
    // Same floor as IAlgorithmSerializer::deserialize: fixed header (29) +
    // empty-tag overhead (8).  Anything smaller cannot be a checkpoint.
    constexpr size_t MIN_CHECKPOINT_BYTES = 37;
    if (data.size() < MIN_CHECKPOINT_BYTES)
        return {};

    checkpoint::Checkpoint cp;
    ByteStreamReader r(data.data(), data.size());
    r >> cp;
    if (!r.ok() || !cp.verify())
        return {};

    // A valid checkpoint always carries a non-empty algorithm tag (the
    // serializer stamps it from algo.name()); the default-constructed
    // MetaHeader — with its empty tag — doubles as the failure marker.
    if (cp.meta.algorithm_tag.empty())
        return {};
    return cp.meta;
}

} // namespace algorithm
