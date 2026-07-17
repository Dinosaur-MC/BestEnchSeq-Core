#pragma once
#include "algorithm/serialization/CompactSerializer.h"
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

    std::vector<uint8_t> serialize(const IAlgorithm& algo) const;
    void deserialize(IAlgorithm& algo, std::span<const uint8_t> data) const;

protected:
    // ── Common sections (base implementation) ────────────────────────

    /// info.len indicates payload size.  The reader is positioned at
    /// the start of payload.  Override to handle additional common
    /// section types.
    virtual void _read_common_sections(ByteStreamReader& r,
                                       const compact_serial::SectionInfo& info,
                                       IAlgorithm& algo) const;

    virtual void _write_common_sections(ByteStreamWriter& w,
                                        const IAlgorithm& algo,
                                        uint32_t& next_section_id) const;

    // ── Algorithm-specific sections (pure virtual) ───────────────────

    /// Reads all algorithm-specific sections from the stream.
    /// The reader is positioned after the file header and any common
    /// sections have already been consumed.  Only algorithm sections
    /// (type MSB=1) remain in the stream.
    virtual void _read_algo_sections(ByteStreamReader& r,
                                     IAlgorithm& algo) const = 0;

    virtual void _write_algo_sections(ByteStreamWriter& w,
                                      const IAlgorithm& algo,
                                      uint32_t& next_section_id) const = 0;
};
