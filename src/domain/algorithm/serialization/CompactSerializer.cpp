#include "CompactSerializer.h"
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
    if (!r.ok() || tag_len > r.remaining()) { hdr.magic = 0; return hdr; }
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

void write(ByteStreamWriter& w, const Ench& e) {
    w.i16(e.id);
    w.i16(e.level);
}

Ench read_ench(ByteStreamReader& r) {
    Ench e;
    e.id    = r.i16();
    e.level = r.i16();
    return e;
}

// ── EnchSet (u32 count + Ench[count]) ───────────────────────────────────

void write(ByteStreamWriter& w, const EnchSet& s) {
    w.u32(static_cast<uint32_t>(s.size()));
    for (const auto& ench : s)
        write(w, ench);
}

EnchSet read_ench_set(ByteStreamReader& r) {
    EnchSet result;
    uint32_t count = r.u32();
    if (count > MAX_SERIAL_ENCHES) { r.set_fail(); return result; }
    for (uint32_t i = 0; i < count; ++i) {
        result.insert(read_ench(r));
        if (!r.ok()) break;
    }
    return result;
}

// ── Item (u8 type + i16 dur + u8 ppn + EnchSet) ─────────────────────────

void write(ByteStreamWriter& w, const Item& item) {
    w.u8(static_cast<uint8_t>(item.type));
    w.i16(item.dur);
    w.u8(item.ppn);
    write(w, item.enchs);
}

Item read_item(ByteStreamReader& r) {
    Item item;
    item.type = static_cast<ItemType>(r.u8());
    item.dur  = r.i16();
    item.ppn  = r.u8();
    item.enchs = read_ench_set(r);
    return item;
}

// ── EnchStep (Item base + Item sacrifice + i32 cost) ────────────────────

void write(ByteStreamWriter& w, const EnchStep& step) {
    write(w, step.base);
    write(w, step.sacrifice);
    w.i32(step.cost);
}

EnchStep read_ench_step(ByteStreamReader& r) {
    EnchStep step;
    step.base      = read_item(r);
    step.sacrifice = read_item(r);
    step.cost      = r.i32();
    return step;
}

// ── EnchSolution (u32 num_steps + EnchStep[num_steps] + i32 total_cost) ─

void write(ByteStreamWriter& w, const EnchSolution& sol) {
    w.u32(static_cast<uint32_t>(sol.steps.size()));
    for (const auto& step : sol.steps)
        write(w, step);
    w.i32(sol.total_cost);
}

EnchSolution read_ench_solution(ByteStreamReader& r) {
    EnchSolution sol;
    uint32_t num_steps = r.u32();
    if (num_steps > MAX_SERIAL_STEPS) { r.set_fail(); return sol; }
    sol.steps.reserve(num_steps);
    for (uint32_t i = 0; i < num_steps; ++i) {
        sol.steps.push_back(read_ench_step(r));
        if (!r.ok()) break;
    }
    sol.total_cost = r.i32();
    return sol;
}

// ── ForgeConfig (u8 flags) ───────────────────────────────────────────────

void write(ByteStreamWriter& w, const ForgeConfig& c) {
    uint8_t flags = 0;
    if (c.ignore_penalty_cost) flags |= 0x01;
    if (c.ignore_repair_cost)  flags |= 0x02;
    if (c.ignore_cost_cap)     flags |= 0x04;
    flags |= static_cast<uint8_t>((static_cast<uint8_t>(c.platform) & 0x0F) << 4);
    w.u8(flags);
}

ForgeConfig read_forge_config(ByteStreamReader& r) {
    ForgeConfig c;
    uint8_t flags = r.u8();
    if (!r.ok()) return c;
    c.ignore_penalty_cost = (flags & 0x01) != 0;
    c.ignore_repair_cost  = (flags & 0x02) != 0;
    c.ignore_cost_cap     = (flags & 0x04) != 0;
    c.platform = static_cast<MCE>((flags >> 4) & 0x0F);
    return c;
}

// ── SearchConfig (i32×3 + i64) ────────────────────────────────────────────

void write(ByteStreamWriter& w, const SearchConfig& c) {
    w.i32(c.max_solutions);
    w.i32(c.max_depth);
    w.i32(c.memory_mb);
    w.i64(static_cast<int64_t>(c.max_search_time.count()));
}

SearchConfig read_search_config(ByteStreamReader& r) {
    SearchConfig c;
    c.max_solutions = r.i32();
    if (!r.ok()) return c;
    c.max_depth     = r.i32();
    if (!r.ok()) return c;
    c.memory_mb     = r.i32();
    if (!r.ok()) return c;
    c.max_search_time = std::chrono::milliseconds(r.i64());
    return c;
}

// ── Equipment (string + string + i32 + i32) ──────────────────────────────

void write(ByteStreamWriter& w, const Equipment& eq) {
    w.i32(eq.id);
    w.i32(eq.category_id);
    w.i32(eq.max_durability);
}

Equipment read_equipment(ByteStreamReader& r) {
    Equipment eq;
    eq.id = r.i32();
    if (!r.ok()) return eq;
    eq.category_id = r.i32();
    if (!r.ok()) return eq;
    eq.max_durability = r.i32();
    return eq;
}

// ── EnchInfo (domain type) ────────────────────────────────────────────────

// void write(ByteStreamWriter& w, const EnchInfo& info) {
//     w.string(info.name_id);
//     w.string(info.name);
//     w.i8(static_cast<int8_t>(info.supported_platform));
//     w.i32(info.max_level);
//     w.i32(info.limited_level);
//     w.i32(info.multiplier);
//     w.u8(info.is_treasure ? 1 : 0);

//     w.u32(static_cast<uint32_t>(info.exclusive_set.size()));
//     for (const auto& s : info.exclusive_set)
//         w.string(s);

//     w.u32(static_cast<uint32_t>(info.applicable_category_ids.size()));
//     for (auto id : info.applicable_category_ids)
//         w.i32(id);
// }

// EnchInfo read_ench_info(ByteStreamReader& r) {
//     EnchInfo info;
//     info.name_id = r.string();
//     if (!r.ok()) return info;
//     info.name = r.string();
//     if (!r.ok()) return info;
//     info.supported_platform = static_cast<MCE>(r.i8());
//     if (!r.ok()) return info;
//     info.max_level = r.i32();
//     if (!r.ok()) return info;
//     info.limited_level = r.i32();
//     if (!r.ok()) return info;
//     info.multiplier = r.i32();
//     if (!r.ok()) return info;
//     info.is_treasure = r.u8() != 0;
//     if (!r.ok()) return info;

//     // exclusive_set — clamp count against MAX_SERIAL_ENCHES
//     {
//         uint32_t excl_size = r.u32();
//         if (excl_size > MAX_SERIAL_ENCHES) { r.set_fail(); return info; }
//         for (uint32_t i = 0; i < excl_size; ++i) {
//             auto s = r.string();
//             if (!r.ok()) break;
//             info.exclusive_set.insert(std::move(s));
//         }
//     }
//     if (!r.ok()) return info;

//     // applicable_category_ids — clamp count against MAX_SERIAL_ENCHES
//     {
//         uint32_t cat_size = r.u32();
//         if (cat_size > MAX_SERIAL_ENCHES) { r.set_fail(); return info; }
//         for (uint32_t i = 0; i < cat_size; ++i) {
//             info.applicable_category_ids.insert(r.i32());
//             if (!r.ok()) break;
//         }
//     }

//     return info;
// }

// ── EnchantmentRegistry (5 data members, count-clamped) ─────────────────

// void write(ByteStreamWriter& w, const EnchantmentRegistry& reg) {
//     // instances_
//     w.u32(static_cast<uint32_t>(reg.instances_.size()));
//     for (const auto& info : reg.instances_)
//         write(w, info);

//     // name_to_index_
//     w.u32(static_cast<uint32_t>(reg.name_to_index_.size()));
//     for (const auto& [key, val] : reg.name_to_index_) {
//         w.string(key);
//         w.i32(val);
//     }

//     // incompatible_table_
//     w.u32(static_cast<uint32_t>(reg.incompatible_table_.size()));
//     for (const auto& [key, set] : reg.incompatible_table_) {
//         w.i32(key);
//         w.u32(static_cast<uint32_t>(set.size()));
//         for (auto v : set)
//             w.i32(v);
//     }

//     // local_to_global_
//     w.u32(static_cast<uint32_t>(reg.local_to_global_.size()));
//     for (auto v : reg.local_to_global_)
//         w.i32(v);

//     // global_to_local_
//     w.u32(static_cast<uint32_t>(reg.global_to_local_.size()));
//     for (const auto& [key, val] : reg.global_to_local_) {
//         w.i32(key);
//         w.i32(val);
//     }
// }

// EnchantmentRegistry read_enchantment_registry(ByteStreamReader& r) {
//     EnchantmentRegistry reg;

//     // instances_
//     {
//         uint32_t n = r.u32();
//         if (n > MAX_SERIAL_ENCHES) { r.set_fail(); return reg; }
//         reg.instances_.reserve(n);
//         for (uint32_t i = 0; i < n; ++i) {
//             reg.instances_.push_back(read_ench_info(r));
//             if (!r.ok()) break;
//         }
//     }
//     if (!r.ok()) { reg.instances_.clear(); return reg; }

//     // name_to_index_
//     {
//         uint32_t n = r.u32();
//         if (n > MAX_SERIAL_ENCHES) { r.set_fail(); return reg; }
//         for (uint32_t i = 0; i < n; ++i) {
//             auto key = r.string();
//             int32_t val = r.i32();
//             if (!r.ok()) break;
//             reg.name_to_index_.emplace(std::move(key), val);
//         }
//     }
//     if (!r.ok()) { reg.instances_.clear(); return reg; }

//     // incompatible_table_
//     {
//         uint32_t n = r.u32();
//         if (n > MAX_SERIAL_ENCHES) { r.set_fail(); return reg; }
//         for (uint32_t i = 0; i < n; ++i) {
//             int32_t key = r.i32();
//             uint32_t set_size = r.u32();
//             if (set_size > MAX_SERIAL_ENCHES) { r.set_fail(); return reg; }
//             std::unordered_set<int32_t> set;
//             for (uint32_t j = 0; j < set_size; ++j) {
//                 set.insert(r.i32());
//                 if (!r.ok()) break;
//             }
//             if (!r.ok()) break;
//             reg.incompatible_table_.emplace(key, std::move(set));
//         }
//     }
//     if (!r.ok()) { reg.instances_.clear(); return reg; }

//     // local_to_global_
//     {
//         uint32_t n = r.u32();
//         if (n > MAX_SERIAL_ENCHES) { r.set_fail(); return reg; }
//         reg.local_to_global_.reserve(n);
//         for (uint32_t i = 0; i < n; ++i) {
//             reg.local_to_global_.push_back(r.i32());
//             if (!r.ok()) break;
//         }
//     }
//     if (!r.ok()) { reg.instances_.clear(); return reg; }

//     // global_to_local_
//     {
//         uint32_t n = r.u32();
//         if (n > MAX_SERIAL_ENCHES) { r.set_fail(); return reg; }
//         for (uint32_t i = 0; i < n; ++i) {
//             int32_t key = r.i32();
//             int32_t val = r.i32();
//             if (!r.ok()) break;
//             reg.global_to_local_.emplace(key, val);
//         }
//     }
//     if (!r.ok()) { reg.instances_.clear(); return reg; }

//     return reg;
// }

// ── EnchInfo (u16×3 + u32 + [u64]*N + u8) ──────────────────────

void write(ByteStreamWriter& w, const EnchInfo& info) {
    w.u16(info.mul);
    w.u16(info.mul_b);
    w.u16(info.max_lvl);
    w.u32(static_cast<uint32_t>(info.exc_mask.size()));
    for (auto m : info.exc_mask)
        w.u64(m);
    w.u8(info.applicable ? 1 : 0);
}

EnchInfo read_compact_ench_info(ByteStreamReader& r) {
    EnchInfo info;
    info.mul    = r.u16();
    info.mul_b  = r.u16();
    info.max_lvl = r.u16();
    uint32_t mask_size = r.u32();
    if (mask_size > MAX_SERIAL_ENCHES) { r.set_fail(); return info; }
    info.exc_mask.resize(mask_size);
    for (uint32_t i = 0; i < mask_size; ++i) {
        info.exc_mask[i] = r.u64();
        if (!r.ok()) break;
    }
    info.applicable = r.u8() != 0;
    return info;
}

// ── EnchReg ─────────────────────────────────────────────────────

// void write(ByteStreamWriter& w, const EnchReg& reg) {
//     // _registry
//     write(w, reg._registry);

//     // _ench_infos (EnchInfo)
//     w.u32(static_cast<uint32_t>(reg._ench_infos.size()));
//     for (const auto& ci : reg._ench_infos)
//         write(w, ci);

//     // _target_equip
//     write(w, reg._target_equip);

//     // _mask_size
//     w.u64(static_cast<uint64_t>(reg._mask_size));

//     // _conflict_matrix
//     w.u32(static_cast<uint32_t>(reg._conflict_matrix.size()));
//     w.bytes(reg._conflict_matrix.data(), reg._conflict_matrix.size());
// }

// EnchReg read_ench_reg(ByteStreamReader& r) {
//     EnchReg reg;

//     // _registry
//     reg._registry = read_enchantment_registry(r);
//     if (!r.ok()) return reg;

//     // _ench_infos
//     {
//         uint32_t n = r.u32();
//         if (n > MAX_SERIAL_ENCHES) { r.set_fail(); return reg; }
//         reg._ench_infos.resize(n);
//         for (uint32_t i = 0; i < n; ++i) {
//             reg._ench_infos[i] = read_compact_ench_info(r);
//             if (!r.ok()) break;
//         }
//     }
//     if (!r.ok()) return reg;

//     // _target_equip
//     reg._target_equip = read_equipment(r);
//     if (!r.ok()) return reg;

//     // _mask_size
//     reg._mask_size = static_cast<size_t>(r.u64());
//     if (!r.ok()) return reg;

//     // _conflict_matrix — verify N×N shape vs _ench_infos
//     {
//         uint32_t n = r.u32();
//         size_t dim = reg._ench_infos.size();
//         if (dim == 0) {
//             // No enchantments — conflict matrix must also be empty
//             if (n != 0) { r.set_fail(); return reg; }
//         } else {
//             if (dim > SIZE_MAX / dim) { r.set_fail(); return reg; }
//             size_t expected = dim * dim;
//             if (static_cast<size_t>(n) != expected) { r.set_fail(); return reg; }
//             reg._conflict_matrix.resize(n);
//             for (uint32_t i = 0; i < n; ++i) {
//                 reg._conflict_matrix[i] = static_cast<char>(r.u8());
//                 if (!r.ok()) break;
//             }
//         }
//     }

//     return reg;
// }

// ── AlgorithmInput ────────────────────────────────────────────────────────

void write(ByteStreamWriter& w, const AlgorithmInput& input) {
    write(w, input.config);
    write(w, input.search);

    // items
    w.u32(static_cast<uint32_t>(input.items.size()));
    for (const auto& item : input.items)
        write(w, item);

    // target
    w.u32(static_cast<uint32_t>(input.target.size()));
    for (const auto& ench : input.target)
        write(w, ench);

    write(w, input.ench_reg);
    w.i32(input.initial_bound);
}

AlgorithmInput read_algorithm_input(ByteStreamReader& r) {
    AlgorithmInput input;

    input.config = read_forge_config(r);
    if (!r.ok()) return input;

    input.search = read_search_config(r);
    if (!r.ok()) return input;

    // items
    {
        uint32_t n = r.u32();
        if (n > MAX_SERIAL_ITEMS) { r.set_fail(); return input; }
        input.items.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            input.items.push_back(read_item(r));
            if (!r.ok()) break;
        }
    }
    if (!r.ok()) return input;

    // target
    {
        uint32_t n = r.u32();
        if (n > MAX_SERIAL_ENCHES) { r.set_fail(); return input; }
        input.target.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            input.target.push_back(read_ench(r));
            if (!r.ok()) break;
        }
    }
    if (!r.ok()) return input;

    input.ench_reg = read_ench_reg(r);
    if (!r.ok()) return input;

    input.initial_bound = r.i32();

    return input;
}

} // namespace compact_serial
