#pragma once
#include <cstddef>
#include <cstdint>
#include "io/ByteStream.h"
#include "types/CompactedTypes.h"

// ─── Non-intrusive serialization primitives for compact types ───────────
//
// All functions live in namespace compact_serial and operate on compact
// types without modifying their definitions.  Serialization is
// little-endian via ByteStreamWriter / ByteStreamReader.
//
// Section header format:
//   [8 bytes tag] + [u32 version] + [u32 section_id] + [u32 payload_len]

namespace compact_serial {

// ── Section header constants ────────────────────────────────────────────

inline constexpr size_t HEADER_TAG_SIZE = 8;
inline constexpr uint32_t CURRENT_VERSION = 1;

enum SectionId : uint32_t {
    SECTION_ITEM_POOL = 1,
    SECTION_STEP_POOL = 2,
    SECTION_OPEN_HEAP = 3,
    SECTION_BEST_G    = 4,
    SECTION_SCALARS   = 5,
};

// ── Section header ──────────────────────────────────────────────────────

/// Writes a section header: tag "BESQ_AS1" (8 bytes) + version (u32) +
/// section_id (u32) + payload_len (u32).
void write_section_header(ByteStreamWriter& w, SectionId id, uint32_t payload_len);

/// Reads and validates a section header.  On success the reader is advanced
/// past the header fields and positioned at the payload start.  On mismatch
/// the function returns false; the caller is expected to check the return
/// value and handle errors.
bool check_section_header(ByteStreamReader& r, SectionId expected_id);

// ── Compact type serialization ──────────────────────────────────────────

void write(ByteStreamWriter& w, const compact::Ench& e);
compact::Ench read_ench(ByteStreamReader& r);

void write(ByteStreamWriter& w, const compact::EnchSet& s);
compact::EnchSet read_ench_set(ByteStreamReader& r);

void write(ByteStreamWriter& w, const compact::Item& item);
compact::Item read_item(ByteStreamReader& r);

void write(ByteStreamWriter& w, const compact::EnchStep& step);
compact::EnchStep read_ench_step(ByteStreamReader& r);

void write(ByteStreamWriter& w, const compact::EnchSolution& sol);
compact::EnchSolution read_ench_solution(ByteStreamReader& r);

// ── Container helpers ────────────────────────────────────────────────

/// Write a vector of elements that have a compact_serial::write() overload.
/// Layout: u32(count) + [T]×count
template <typename T>
void write_vector(ByteStreamWriter& w, const std::vector<T>& vec) {
    w.u32(static_cast<uint32_t>(vec.size()));
    for (const auto& v : vec)
        write(w, v);
}

/// Read a vector of elements that have a compact_serial::read_*(r) function.
/// The caller provides a callable to read each element.
template <typename T, typename Reader>
std::vector<T> read_vector(ByteStreamReader& r, Reader&& read_one) {
    uint32_t n = r.u32();
    std::vector<T> vec;
    vec.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        vec.push_back(read_one(r));
        if (!r.ok()) break;
    }
    return vec;
}

} // namespace compact_serial
