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

// File-level constants
inline constexpr uint32_t FILE_MAGIC   = 0x51534542; // "BESQ" LE
inline constexpr uint16_t FILE_VERSION = 1;

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

    void serialize(ByteStreamWriter& w) const noexcept override;
    void deserialize(ByteStreamReader& r) noexcept override;
};

void compute_crc56(const uint8_t* data, size_t len, uint8_t crc[7]);

} // namespace checkpoint
