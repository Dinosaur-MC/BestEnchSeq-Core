#include "algorithm/serialization/CompactSerializer.h"
#include <cstring>
#include <chrono>

namespace compact_serial {

// ── CRC-56 helper ───────────────────────────────────────────────────────
//
// Simple 56-bit checksum: for each byte, XOR into one accumulator and
// ADD into another, rotating across 7 lanes.  Adequate for accidental
// corruption detection within checkpoint files.

void compute_crc56(const uint8_t* data, size_t len, uint8_t crc[7]) {
    std::memset(crc, 0, 7);
    for (size_t i = 0; i < len; ++i) {
        size_t lane = i % 7;
        crc[lane]       = static_cast<uint8_t>(crc[lane] ^ data[i]);
        crc[(lane+1)%7] = static_cast<uint8_t>(crc[(lane+1)%7] + data[i]);
    }
}

// ── File-level header ───────────────────────────────────────────────────

void write_file_header(ByteStreamWriter& w, const FileHeader& hdr) {
    w.u32(hdr.magic);
    w.u16(hdr.version);
    w.u16(hdr.flags);
    w.u32(hdr.num_sections);
    w.i64(hdr.timestamp);
    w.bytes(hdr.crc_code, 7);
    w.u16(hdr.algo_version);

    auto tag_len = static_cast<uint8_t>(hdr.algorithm_tag.size());
    w.u8(tag_len);
    w.bytes(hdr.algorithm_tag.data(), tag_len);
}

FileHeader read_file_header(ByteStreamReader& r) {
    FileHeader hdr{};

    hdr.magic        = r.u32();
    hdr.version      = r.u16();
    hdr.flags        = r.u16();
    hdr.num_sections = r.u32();
    hdr.timestamp    = r.i64();
    for (int i = 0; i < 7; ++i)
        hdr.crc_code[i] = r.u8();
    hdr.algo_version = r.u16();

    uint8_t tag_len = r.u8();
    if (!r.ok()) { hdr.magic = 0; return hdr; }
    hdr.algorithm_tag.resize(tag_len);
    for (uint8_t i = 0; i < tag_len; ++i)
        hdr.algorithm_tag[i] = static_cast<char>(r.u8());

    if (!r.ok() || hdr.magic != FILE_MAGIC || hdr.version != FILE_VERSION)
        hdr.magic = 0;

    return hdr;
}

FileHeader peek_file_header(const uint8_t* data, size_t size) {
    ByteStreamReader r(data, size);
    return read_file_header(r);
}

// ── Section header ──────────────────────────────────────────────────────

void write_section_header(ByteStreamWriter& w, uint32_t type, uint32_t section_id, uint64_t payload_len) {
    w.u32(type);
    w.u32(0);          // flags (reserved)
    w.u32(section_id);
    w.u64(payload_len);
}

SectionInfo read_section_header(ByteStreamReader& r) {
    SectionInfo si{};
    si.type       = r.u32();
    /*flags*/ r.u32();
    si.section_id = r.u32();
    si.len        = r.u64();
    if (!r.ok()) {
        si.type = 0; si.section_id = 0; si.len = 0;
    }
    return si;
}

// ── Ench (4 bytes: i16 id + i16 level) ──────────────────────────────────

void write(ByteStreamWriter& w, const compact::Ench& e) {
    w.i16(e.id);
    w.i16(e.level);
}

compact::Ench read_ench(ByteStreamReader& r) {
    compact::Ench e;
    e.id    = r.i16();
    e.level = r.i16();
    return e;
}

// ── EnchSet (u32 count + Ench[count]) ───────────────────────────────────

void write(ByteStreamWriter& w, const compact::EnchSet& s) {
    w.u32(static_cast<uint32_t>(s.size()));
    for (const auto& ench : s)
        write(w, ench);
}

compact::EnchSet read_ench_set(ByteStreamReader& r) {
    compact::EnchSet result;
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i) {
        result.insert(read_ench(r));
        if (!r.ok()) break;
    }
    return result;
}

// ── Item (u8 type + i16 dur + u8 ppn + EnchSet) ─────────────────────────

void write(ByteStreamWriter& w, const compact::Item& item) {
    w.u8(static_cast<uint8_t>(item.type));
    w.i16(item.dur);
    w.u8(item.ppn);
    write(w, item.enchs);
}

compact::Item read_item(ByteStreamReader& r) {
    compact::Item item;
    item.type = static_cast<compact::ItemType>(r.u8());
    item.dur  = r.i16();
    item.ppn  = r.u8();
    item.enchs = read_ench_set(r);
    return item;
}

// ── EnchStep (Item base + Item sacrifice + i32 cost) ────────────────────

void write(ByteStreamWriter& w, const compact::EnchStep& step) {
    write(w, step.base);
    write(w, step.sacrifice);
    w.i32(step.cost);
}

compact::EnchStep read_ench_step(ByteStreamReader& r) {
    compact::EnchStep step;
    step.base      = read_item(r);
    step.sacrifice = read_item(r);
    step.cost      = r.i32();
    return step;
}

// ── EnchSolution (u32 num_steps + EnchStep[num_steps] + i32 total_cost) ─

void write(ByteStreamWriter& w, const compact::EnchSolution& sol) {
    w.u32(static_cast<uint32_t>(sol.steps.size()));
    for (const auto& step : sol.steps)
        write(w, step);
    w.i32(sol.total_cost);
}

compact::EnchSolution read_ench_solution(ByteStreamReader& r) {
    compact::EnchSolution sol;
    uint32_t num_steps = r.u32();
    sol.steps.reserve(num_steps);
    for (uint32_t i = 0; i < num_steps; ++i) {
        sol.steps.push_back(read_ench_step(r));
        if (!r.ok()) break;
    }
    sol.total_cost = r.i32();
    return sol;
}

} // namespace compact_serial
