#pragma once
#include "../BESQTypes.h"
#include "../registries/EquipmentRegistry.h"
#include "../registries/RegistryAccess.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ─── Binary Serializer ───
// Simple little-endian binary serializer. Appends to internal buffer.
// Header: 8 bytes (magic "BESQ" + version uint32_t).
class Serializer {
public:
    static constexpr uint32_t MAGIC = 0x51534542;  // "BESQ" little-endian
    static constexpr uint32_t VERSION = 1;

    Serializer() { _buf.reserve(4096); }

    // ── Primitives ──
    void u8(uint8_t v) { _buf.push_back(v); }
    void u32(uint32_t v) {
        _buf.push_back(static_cast<uint8_t>(v));
        _buf.push_back(static_cast<uint8_t>(v >> 8));
        _buf.push_back(static_cast<uint8_t>(v >> 16));
        _buf.push_back(static_cast<uint8_t>(v >> 24));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void bytes(const void* data, size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        _buf.insert(_buf.end(), p, p + len);
    }
    void string(std::string_view s) {
        u32(static_cast<uint32_t>(s.size()));
        bytes(s.data(), s.size());
    }

    // ── Domain types ──
    void write(const Ench& e) {
        i32(e.id);
        i32(e.level);
    }
    void write(const EnchSet& s) {
        // Canonical order: sort by id for deterministic output.
        std::vector<Ench> sorted(s.begin(), s.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const Ench& a, const Ench& b) { return a.id < b.id; });
        u32(static_cast<uint32_t>(sorted.size()));
        for (const auto& e : sorted) write(e);
    }
    void write(const ItemStack& item) {
        // Equipment: store as name_id string (empty if book)
        if (item.equipment) {
            string(item.equipment->name_id);
        } else {
            string(std::string_view{});
        }
        write(item.enchantments);
        i32(item.prior_penalty);
        i32(item.durability);
        i32(item.priority);
        // _cache is derived — not serialized
    }
    void write(const EnchSolution::EnchStep& step) {
        write(step.item_a);
        write(step.item_b);
        i32(step.exp_level_cost);
        i32(step.exp_cost);
    }
    void write(const std::vector<int32_t>& vec) {
        u32(static_cast<uint32_t>(vec.size()));
        for (auto v : vec) i32(v);
    }

    // ── Result ──
    const std::vector<uint8_t>& data() const { return _buf; }
    std::vector<uint8_t> take() && { return std::move(_buf); }
    void clear() { _buf.clear(); }

private:
    std::vector<uint8_t> _buf;
};

// ─── Binary Deserializer ───
// Reads from a byte vector with bounds checking.
class Deserializer {
public:
    explicit Deserializer(const std::vector<uint8_t>& data)
        : _pos(data.data()), _end(data.data() + data.size()), _ok(true) {}

    // ── Status ──
    bool ok() const { return _ok; }
    bool has_more() const { return _ok && _pos < _end; }
    const uint8_t* pos() const { return _pos; }

    // ── Primitives ──
    uint8_t u8() {
        if (!_ok || _pos + 1 > _end) { _ok = false; return 0; }
        return *_pos++;
    }
    uint32_t u32() {
        if (!_ok || _pos + 4 > _end) { _ok = false; return 0; }
        uint32_t v = static_cast<uint32_t>(_pos[0])
                   | static_cast<uint32_t>(_pos[1]) << 8
                   | static_cast<uint32_t>(_pos[2]) << 16
                   | static_cast<uint32_t>(_pos[3]) << 24;
        _pos += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    std::string string() {
        uint32_t len = u32();
        if (!_ok || _pos + len > _end) { _ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(_pos), len);
        _pos += len;
        return s;
    }

    // ── Domain types ──
    Ench read_ench() {
        int32_t id = i32();
        int32_t level = i32();
        if (!_ok) return Ench(0, 0, Ench::unchecked);
        return Ench(id, level, Ench::unchecked);
    }
    EnchSet read_ench_set() {
        EnchSet result;
        uint32_t count = u32();
        for (uint32_t i = 0; i < count; ++i) {
            Ench e = read_ench();
            if (!_ok) break;
            result.insert(e);
        }
        return result;
    }
    ItemStack read_item_stack() {
        std::string eq_id = string();
        EnchSet ench = read_ench_set();
        int32_t pp = i32();
        int32_t dur = i32();
        int32_t prio = i32();
        if (!_ok) return {};

        if (!eq_id.empty()) {
            int32_t eid = registries::equipment().get_id(eq_id);
            if (eid >= 0) {
                const Equipment& eq_ref = registries::equipment().get(eid);
                ItemStack item(eq_ref, ench, pp, dur);
                item.priority = prio;
                return item;
            }
        }
        // Book or unknown equipment — construct as book
        ItemStack item(ench, pp);
        item.durability = dur;
        item.priority = prio;
        return item;
    }
    EnchSolution::EnchStep read_step() {
        ItemStack a = read_item_stack();
        ItemStack b = read_item_stack();
        int32_t cost = i32();
        int32_t exp = i32();
        if (!_ok) return {};
        return {a, b, cost, exp};
    }
    std::vector<int32_t> read_i32_vec() {
        uint32_t count = u32();
        std::vector<int32_t> vec;
        vec.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            vec.push_back(i32());
            if (!_ok) break;
        }
        return vec;
    }

private:
    const uint8_t* _pos;
    const uint8_t* _end;
    bool _ok;
};
