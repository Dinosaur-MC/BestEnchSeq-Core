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

} // namespace compact_serial
