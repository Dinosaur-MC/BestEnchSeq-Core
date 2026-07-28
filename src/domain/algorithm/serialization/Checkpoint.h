#pragma once
#include "common/serialization/IBinarySerializable.h"
#include <cstdint>
#include <string>
#include <vector>

namespace checkpoint {

// Section type flags
inline constexpr uint32_t SECTION_TYPE_COMMON = 0x00000000u;
inline constexpr uint32_t SECTION_TYPE_ALGO   = 0x80000000u;
inline constexpr uint32_t SECTION_TYPE_INPUT  = 0x00000001u;

/// Combine a raw section tag with the SECTION_TYPE_ALGO bit.
inline constexpr uint32_t make_algo_tag(uint32_t raw_tag) noexcept {
    return SECTION_TYPE_ALGO | raw_tag;
}
/// Extract the raw section tag from a section header type.
inline constexpr uint32_t get_algo_tag(uint32_t raw_type) noexcept {
    return raw_type & ~SECTION_TYPE_ALGO;
}

// File-level constants
inline constexpr uint32_t FILE_MAGIC   = 0x51534542; // "BESQ" LE
inline constexpr uint16_t FILE_VERSION = 1;
inline constexpr uint32_t MAX_CHECKPOINT_SECTIONS = 256;

struct MetaHeader : IBinarySerializable {
    uint32_t magic         = FILE_MAGIC;
    uint16_t version       = FILE_VERSION;
    uint16_t flags         = 0;
    uint32_t num_sections  = 0;
    int64_t  timestamp     = 0;
    uint8_t  crc_code[7]   = {};
    uint16_t algo_version  = 0;
    std::string algorithm_tag;

    MetaHeader() = default;
    MetaHeader(std::string_view tag, uint16_t algo_ver, uint32_t sect_count);

    void serialize(ByteStreamWriter& w) const noexcept override;
    void deserialize(ByteStreamReader& r) noexcept override;
};

struct SectionHeader : IBinarySerializable {
    uint32_t type       = 0;
    uint32_t section_id = 0;
    uint64_t payload_len = 0;

    SectionHeader() = default;
    SectionHeader(uint32_t t, uint32_t id, uint64_t len);

    void serialize(ByteStreamWriter& w) const noexcept override;
    void deserialize(ByteStreamReader& r) noexcept override;
};

struct Section : IBinarySerializable {
    SectionHeader header;
    std::vector<uint8_t> payload;

    Section() = default;
    Section(uint32_t type, uint32_t id, const IBinarySerializable& body);

    void serialize(ByteStreamWriter& w) const noexcept override;
    void deserialize(ByteStreamReader& r) noexcept override;
};

struct Checkpoint : IBinarySerializable {
    MetaHeader meta;
    std::vector<Section> sections;

    Checkpoint() = default;
    explicit Checkpoint(std::string_view tag, uint16_t algo_ver);

    void add_section(uint32_t type, uint32_t id, const IBinarySerializable& body);

    /// Compute CRC-56 over algorithm_tag + algo_version + all section payloads.
    /// Must be called after all sections are added, before serialize().
    void finalize() noexcept;

    /// Verify CRC-56 after deserialization.  Returns true on match or when
    /// the CRC field is all-zero (legacy checkpoint with no CRC).
    bool verify() const noexcept;

    void serialize(ByteStreamWriter& w) const noexcept override;
    void deserialize(ByteStreamReader& r) noexcept override;
};

void compute_crc56(const uint8_t* data, size_t len, uint8_t crc[7]);

} // namespace checkpoint
