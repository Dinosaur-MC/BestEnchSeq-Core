#include "algorithm/serialization/IAlgorithmSerializer.h"
#include "algorithm/IAlgorithm.h"
#include <chrono>

using namespace compact_serial;

// ─── serialize: base orchestrates ────────────────────────────────────────

std::vector<uint8_t> IAlgorithmSerializer::serialize(const IAlgorithm& algo,
                                                      const AlgorithmInput& input) const {
    ByteStreamWriter w;
    uint32_t next_id = 1;  // file-global section ID counter

    // 1. Common section (algorithm name + version, for forward compat)
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

    // 2. Input section (AlgorithmInput provided explicitly — not from IAlgorithm)
    {
        ByteStreamWriter payload;
        write(payload, input);
        auto p = std::move(payload).take();
        write_section_header(w, SECTION_TYPE_INPUT, next_id++, p.size());
        w.bytes(p.data(), p.size());
    }

    // 3. Algorithm state sections (subclass provides opaque payloads)
    auto algo_sections = _serialize_state(algo);
    for (const auto& sect : algo_sections) {
        // Encode logical section_tag in lower bits of section type
        uint32_t section_type = SECTION_TYPE_ALGO | sect.section_tag;
        write_section_header(w, section_type, next_id++, sect.payload.size());
        w.bytes(sect.payload.data(), sect.payload.size());
    }

    // 4. Build file header
    FileHeader hdr;
    hdr.magic         = FILE_MAGIC;
    hdr.version       = FILE_VERSION;
    hdr.flags         = 0;
    hdr.num_sections  = next_id - 1;
    hdr.timestamp     = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
    hdr.algo_version  = 1;
    hdr.algorithm_tag = std::string(algorithm_name());

    auto section_data = std::move(w).take();
    compute_crc56(section_data.data(), section_data.size(), hdr.crc_code);

    ByteStreamWriter final_w;
    write_file_header(final_w, hdr);
    final_w.bytes(section_data.data(), section_data.size());
    return std::move(final_w).take();
}

// ─── deserialize: base orchestrates ──────────────────────────────────────

bool IAlgorithmSerializer::deserialize(IAlgorithm& algo, AlgorithmInput& out_input,
                                        std::span<const uint8_t> data) const {
    ByteStreamReader r(data.data(), data.size());

    // 1. File header
    auto hdr = read_file_header(r);
    if (!r.ok() || hdr.magic != FILE_MAGIC) return false;

    // 2. Cross-validate algorithm tag
    if (hdr.algorithm_tag != algorithm_name()) return false;

    // 3. Version check
    if (hdr.algo_version > FILE_ALGO_VERSION_MAX) return false;

    // 4. CRC validation
    const uint8_t* section_start = r.pos();
    size_t section_len = r.remaining();
    uint8_t expected_crc[7];
    compute_crc56(section_start, section_len, expected_crc);
    for (int i = 0; i < 7; ++i)
        if (expected_crc[i] != hdr.crc_code[i]) return false;

    // 5. Parse all sections, dispatching by type
    std::vector<AlgoSectionData> algo_sections;

    while (r.has_more()) {
        auto si = read_section_header(r);
        if (!r.ok()) return false;
        if (si.type == 0 && si.section_id == 0) break;

        auto payload = r.read_bytes(static_cast<size_t>(si.len));
        if (!r.ok()) return false;

        if (si.type == SECTION_TYPE_COMMON) {
            // Verify common metadata matches algorithm identity
            ByteStreamReader pr(payload);
            uint16_t name_len = pr.u16();
            if (!pr.ok()) return false;
            std::string common_name(name_len, '\0');
            for (uint16_t i = 0; i < name_len; ++i)
                common_name[i] = static_cast<char>(pr.u8());
            uint16_t ver_len = pr.u16();
            if (!pr.ok()) return false;
            std::string common_ver(ver_len, '\0');
            for (uint16_t i = 0; i < ver_len; ++i)
                common_ver[i] = static_cast<char>(pr.u8());
            // Cross-check against current algorithm identity
            if (common_name != algo.name() || common_ver != algo.version())
                return false;
            continue;
        }

        if (si.type == SECTION_TYPE_INPUT) {
            // Deserialize AlgorithmInput into the provided out parameter
            ByteStreamReader pr(payload);
            out_input = read_algorithm_input(pr);
            if (!pr.ok()) return false;
            continue;
        }

        // Algorithm-specific section -- extract logical tag from type
        if (si.type & SECTION_TYPE_ALGO) {
            uint32_t logical_tag = si.type & ~SECTION_TYPE_ALGO;
            algo_sections.push_back({logical_tag, std::move(payload)});
            continue;
        }

        // Unknown section type -- skip
        continue;
    }

    if (!r.ok()) return false;

    // 6. Pass algorithm sections to subclass
    if (!_deserialize_state(algo, algo_sections)) return false;

    // 7. Final integrity: no trailing data
    if (r.has_more()) return false;
    return true;
}
