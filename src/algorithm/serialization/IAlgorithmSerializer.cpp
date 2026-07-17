#include "algorithm/serialization/IAlgorithmSerializer.h"
#include "algorithm/IAlgorithm.h"
#include <chrono>

using namespace compact_serial;

// ─── Full file serialization ─────────────────────────────────────────────

std::vector<uint8_t> IAlgorithmSerializer::serialize(const IAlgorithm& algo) const {
    ByteStreamWriter w;
    uint32_t next_section_id = 1;

    // 1. Common sections + algorithm-specific sections
    _write_common_sections(w, algo, next_section_id);
    _write_algo_sections(w, algo, next_section_id);

    // 2. Build file header
    FileHeader hdr;
    hdr.magic         = FILE_MAGIC;
    hdr.version       = FILE_VERSION;
    hdr.flags         = 0;
    hdr.num_sections  = next_section_id - 1;
    hdr.timestamp     = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
    hdr.algo_version  = 1;
    hdr.algorithm_tag = std::string(algorithm_name());

    auto section_data = std::move(w).take();
    compute_crc56(section_data.data(), section_data.size(), hdr.crc_code);

    // 3. Prepend header
    ByteStreamWriter final_w;
    write_file_header(final_w, hdr);
    final_w.bytes(section_data.data(), section_data.size());
    return std::move(final_w).take();
}

// ─── Full file deserialization ───────────────────────────────────────────

void IAlgorithmSerializer::deserialize(IAlgorithm& algo, std::span<const uint8_t> data) const {
    ByteStreamReader r(data.data(), data.size());

    // 1. File header
    auto hdr = read_file_header(r);
    if (hdr.magic != FILE_MAGIC) return;

    // 2. CRC validation over section data (everything after header)
    const uint8_t* section_start = r.pos();
    size_t section_len = r.remaining();
    uint8_t expected_crc[7];
    compute_crc56(section_start, section_len, expected_crc);
    for (int i = 0; i < 7; ++i)
        if (expected_crc[i] != hdr.crc_code[i]) return;

    // 3. Pass the raw section stream to the algorithm-specific reader
    //    which is responsible for parsing all sections (common + algo).
    _read_algo_sections(r, algo);
}

// ─── Common sections ─────────────────────────────────────────────────────

void IAlgorithmSerializer::_write_common_sections(
    ByteStreamWriter& w, const IAlgorithm& algo, uint32_t& next_id) const
{
    auto name_str = std::string(algo.name());
    auto ver_str  = std::string(algo.version());

    ByteStreamWriter payload;
    payload.u16(static_cast<uint16_t>(name_str.size()));
    payload.bytes(name_str.data(), name_str.size());
    payload.u16(static_cast<uint16_t>(ver_str.size()));
    payload.bytes(ver_str.data(), ver_str.size());

    auto p = std::move(payload).take();
    write_section_header(w, SECTION_TYPE_COMMON, next_id++, p.size());
    w.bytes(p.data(), p.size());
}

void IAlgorithmSerializer::_read_common_sections(
    ByteStreamReader& r, const SectionInfo& info, IAlgorithm& algo) const
{
    uint16_t name_len = r.u16();
    if (name_len + 4 > info.len) return;
    for (uint16_t i = 0; i < name_len; ++i) (void)r.u8();
    uint16_t ver_len = r.u16();
    (void)ver_len;
    if (!r.ok()) return;
}
