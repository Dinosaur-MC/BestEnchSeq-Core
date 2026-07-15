#include "algorithm/serialization/CompactSerializer.h"
#include <cstring>

namespace compact_serial {

// ── Section header ──────────────────────────────────────────────────────

void write_section_header(ByteStreamWriter& w, SectionId id, uint32_t payload_len) {
    w.bytes("BESQ_AS1", HEADER_TAG_SIZE);
    w.u32(CURRENT_VERSION);
    w.u32(static_cast<uint32_t>(id));
    w.u32(payload_len);
}

bool check_section_header(ByteStreamReader& r, SectionId expected_id) {
    // Read tag (8 bytes)
    char tag[HEADER_TAG_SIZE];
    for (size_t i = 0; i < HEADER_TAG_SIZE; ++i)
        tag[i] = static_cast<char>(r.u8());
    if (!r.ok()) return false;

    uint32_t ver = r.u32();
    uint32_t sid = r.u32();
    uint32_t len = r.u32();
    if (!r.ok()) return false;

    // Validate tag, version, and section id
    if (std::memcmp(tag, "BESQ_AS1", HEADER_TAG_SIZE) != 0 ||
        ver != CURRENT_VERSION ||
        sid != static_cast<uint32_t>(expected_id)) {
        r.skip(len);    // skip payload to maintain sync
        return false;
    }

    return true;
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
