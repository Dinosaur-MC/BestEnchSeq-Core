#pragma once
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Equipment.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/Solution.h"
#include "common/io/ByteStream.h"
#include <cstddef>
#include <cstdint>
#include <string>

// ─── Non-intrusive serialization primitives for compact types ───────────
//
// All functions live in namespace compact_serial and operate on compact
// types without modifying their definitions.  Serialization is
// little-endian via ByteStreamWriter / ByteStreamReader.
//
// File header layout:
//   magic(4) + ver(2) + flags(2) + num_sects(4) + timestamp(8)
//   + crc_code(7) + alg_ver(2) + tag_len(1) + tag(tag_len)
//
// Section header layout:
//   type(4) + flags(4) + section_id(4) + len(8)

namespace compact_serial {

// ── File-level header ───────────────────────────────────────────────────

inline constexpr uint32_t FILE_MAGIC   = 0x51534542; // "BESQ" LE
inline constexpr uint16_t FILE_VERSION = 1;

/// File-level header stored at offset 0 of every checkpoint file.
struct FileHeader {
    uint32_t magic;            // FILE_MAGIC
    uint16_t version;          // FILE_VERSION
    uint16_t flags;            // reserved, 0
    uint32_t num_sections;     // total section count
    int64_t timestamp;         // unix milliseconds
    uint8_t crc_code[7];       // 56-bit checksum over data after header
    uint16_t algo_version;     // algorithm version (e.g. 1)
    std::string algorithm_tag; // algorithm name, e.g. "astar", len ≤ 255
};

// FileHeader layout is dynamic (std::string member), cannot static_assert.

// ── Section type flags ──────────────────────────────────────────────────

inline constexpr uint32_t SECTION_TYPE_COMMON = 0x00000000u; // MSB=0: shared across algorithms
inline constexpr uint32_t SECTION_TYPE_ALGO   = 0x80000000u; // MSB=1: algorithm-specific
inline constexpr uint32_t SECTION_TYPE_INPUT  = 0x00000001u; // algorithm input section

// Hard upper bounds for deserialized counts (OOM/DoS protection)
inline constexpr uint32_t MAX_SERIAL_ITEMS  = 1'000'000;  // ItemPool, ItemCollection
inline constexpr uint32_t MAX_SERIAL_ENCHES = 100'000;    // EnchSet, EnchCollection
inline constexpr uint32_t MAX_SERIAL_STEPS  = 10'000'000; // StepPool
inline constexpr uint32_t MAX_SERIAL_HEAP   = 10'000'000; // OpenHeap entries
inline constexpr uint32_t MAX_SERIAL_BEST_G = 10'000'000; // FlatHashMap entries
inline constexpr uint32_t MAX_NAME_LEN      = 256;        // algorithm name/version strings

inline constexpr uint16_t FILE_ALGO_VERSION_MAX = 255;

// ── Section data model (opaque payloads for algorithm state) ──────────

/// Opaque algorithm-specific section payload.
/// section_tag: logical identifier meaningful only to the algorithm
///              (e.g. 1=ItemPool, 2=StepPool)
/// payload: raw bytes, no section header wrapping
struct AlgoSectionData {
    uint32_t section_tag;
    std::vector<uint8_t> payload;
};

// ── File header I/O ─────────────────────────────────────────────────────

void write_file_header(ByteStreamWriter &w, const FileHeader &hdr);
FileHeader read_file_header(ByteStreamReader &r);

/// Quick file header parse from raw bytes (no CRC validation).
FileHeader peek_file_header(const uint8_t *data, size_t size);

/// Compute 56-bit checksum over a byte range.  Used to validate
/// checkpoint integrity after the file-level header.
void compute_crc56(const uint8_t *data, size_t len, uint8_t crc[7]);

// ── Section header I/O ─────────────────────────────────────────────────

/// Writes a section header: type(u32) + flags(u32) + section_id(u32) + len(u64)
void write_section_header(ByteStreamWriter &w, uint32_t type, uint32_t section_id, uint64_t payload_len);

/// Reads and validates a section header.  Returns (type, section_id, len)
/// on success, or (0, 0, 0) on failure (reader exhausted).
struct SectionInfo {
    uint32_t type;
    uint32_t section_id;
    uint64_t len;
};
SectionInfo read_section_header(ByteStreamReader &r);

// ── Compact type serialization ──────────────────────────────────────────

void write(ByteStreamWriter &w, const algorithm::Ench &e);
algorithm::Ench read_ench(ByteStreamReader &r);

void write(ByteStreamWriter &w, const algorithm::EnchSet &s);
algorithm::EnchSet read_ench_set(ByteStreamReader &r);

void write(ByteStreamWriter &w, const algorithm::Item &item);
algorithm::Item read_item(ByteStreamReader &r);

void write(ByteStreamWriter &w, const algorithm::EnchStep &step);
algorithm::EnchStep read_ench_step(ByteStreamReader &r);

void write(ByteStreamWriter &w, const algorithm::EnchSolution &sol);
algorithm::EnchSolution read_ench_solution(ByteStreamReader &r);

// ── Configuration types ───────────────────────────────────────────────────

void write(ByteStreamWriter &w, const algorithm::ForgeConfig &c);
algorithm::ForgeConfig read_forge_config(ByteStreamReader &r);

void write(ByteStreamWriter &w, const algorithm::SearchConfig &c);
algorithm::SearchConfig read_search_config(ByteStreamReader &r);

// ── Domain types ──────────────────────────────────────────────────────────

void write(ByteStreamWriter &w, const algorithm::Equipment &eq);
algorithm::Equipment read_equipment(ByteStreamReader &r);

void write(ByteStreamWriter &w, const algorithm::EnchInfo &info);
algorithm::EnchInfo read_ench_info(ByteStreamReader &r);

// ── Compact types (non-primitive) ─────────────────────────────────────────

void write(ByteStreamWriter &w, const algorithm::EnchInfo &info);
algorithm::EnchInfo read_compact_ench_info(ByteStreamReader &r);

void write(ByteStreamWriter &w, const algorithm::EnchReg &reg);
algorithm::EnchReg read_ench_reg(ByteStreamReader &r);

// ── Algorithm I/O ─────────────────────────────────────────────────────────

void write(ByteStreamWriter &w, const algorithm::AlgorithmInput &input);
algorithm::AlgorithmInput read_algorithm_input(ByteStreamReader &r);

// ── Container helpers ────────────────────────────────────────────────

template <typename T> void write_vector(ByteStreamWriter &w, const std::vector<T> &vec) {
    w.u32(static_cast<uint32_t>(vec.size()));
    for (const auto &v : vec)
        write(w, v);
}

template <typename T, typename Reader>
std::vector<T> read_vector(ByteStreamReader &r, Reader &&read_one, uint32_t max_count = MAX_SERIAL_ITEMS) {
    uint32_t n = r.u32();
    if (n > max_count) {
        r.set_fail();
        return {};
    }
    std::vector<T> vec;
    vec.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        vec.push_back(read_one(r));
        if (!r.ok())
            break;
    }
    return vec;
}

} // namespace compact_serial
