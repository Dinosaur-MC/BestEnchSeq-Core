#pragma once
#include "CompactSerializer.h"
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

class IAlgorithm;

class IAlgorithmSerializer {
public:
    virtual ~IAlgorithmSerializer() = default;

    virtual std::string_view algorithm_name() const noexcept = 0;
    virtual std::string_view algorithm_version() const noexcept = 0;

    // ── Full file serialization (non-virtual, base orchestrates) ─────

    /// Serialize algorithm state together with AlgorithmInput into a checkpoint.
    /// The input is written as a dedicated INPUT section owned by the base.
    std::vector<uint8_t> serialize(const IAlgorithm& algo, const AlgorithmInput& input) const;

    /// Deserialize a checkpoint and reconstruct AlgorithmInput + algorithm state.
    /// AlgorithmInput is written to out_input (owned by caller, typically Executor).
    bool deserialize(IAlgorithm& algo, AlgorithmInput& out_input, std::span<const uint8_t> data) const;

protected:
    // ── Algorithm state (subclass implements -- pure data, no headers) ──

    /// Serialize algorithm internal state into opaque sections.
    /// Each section will be wrapped with SECTION_TYPE_ALGO header by the base.
    /// The section_tag is a logical identifier meaningful only to the algorithm.
    /// Returns empty vector if no state to persist.
    virtual std::vector<compact_serial::AlgoSectionData> _serialize_state(const IAlgorithm& algo) const = 0;

    /// Deserialize algorithm internal state from pre-parsed sections.
    /// Only algorithm-specific sections are passed.
    /// Returns true on complete success, false on any failure.
    virtual bool _deserialize_state(IAlgorithm& algo, std::span<const compact_serial::AlgoSectionData> sections) const = 0;
};
