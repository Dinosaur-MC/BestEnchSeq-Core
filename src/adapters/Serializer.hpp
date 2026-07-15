#pragma once
#include "BESQTypes.h"
#include "io/ByteStream.h"
#include "registries/EquipmentRegistry.h"
#include "registries/RegistryAccess.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#ifdef __GNUC__
#define BESQ_DEPRECATED_SERIALIZER __attribute__((deprecated("Use algorithm/serialization/CompactSerializer instead")))
#elif defined(_MSC_VER)
#define BESQ_DEPRECATED_SERIALIZER __declspec(deprecated("Use algorithm/serialization/CompactSerializer instead"))
#else
#define BESQ_DEPRECATED_SERIALIZER
#endif

/// Binary Serializer: BESQ domain types ↔ byte stream.
/// Header constants (magic "BESQ" + version) are defined for external
/// consumers to write/verify as needed.  This class writes plain data
/// without a header — callers prepend header bytes if required.
/// Uses ByteStream for all low-level byte I/O.
BESQ_DEPRECATED_SERIALIZER
class Serializer {
public:
    static constexpr uint32_t MAGIC  = 0x51534542;  // "BESQ" little-endian
    static constexpr uint32_t VERSION = 1;

    Serializer() = default;

    // ── Primitives ──
    void u8(uint8_t v)      { _stream.u8(v); }
    void u32(uint32_t v)    { _stream.u32(v); }
    void i32(int32_t v)     { _stream.i32(v); }
    void bytes(const void* data, size_t len) { _stream.bytes(data, len); }
    void string(std::string_view s) { _stream.string(s); }

    // ── Domain types ──
    void write(const Ench& e) {
        _stream.i32(e.id);
        _stream.i32(e.level);
    }
    void write(const EnchSet& s) {
        std::vector<Ench> sorted(s.begin(), s.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const Ench& a, const Ench& b) { return a.id < b.id; });
        _stream.u32(static_cast<uint32_t>(sorted.size()));
        for (const auto& e : sorted) write(e);
    }
    void write(const ItemStack& item) {
        if (item.equipment) {
            _stream.string(item.equipment->name_id);
        } else {
            _stream.string(std::string_view{});
        }
        write(item.enchantments);
        _stream.i32(item.prior_penalty);
        _stream.i32(item.durability);
        _stream.i32(item.priority);
    }
    void write(const EnchSolution::EnchStep& step) {
        write(step.item_a);
        write(step.item_b);
        _stream.i32(step.exp_level_cost);
        _stream.i32(step.exp_cost);
    }
    void write(const std::vector<int32_t>& vec) {
        _stream.u32(static_cast<uint32_t>(vec.size()));
        for (auto v : vec) _stream.i32(v);
    }

    // ── Result ──
    const std::vector<uint8_t>& data() const& { return _stream.data(); }
    std::vector<uint8_t> take() && { return std::move(_stream).take(); }
    void clear() { _stream.clear(); }

private:
    ByteStreamWriter _stream;
};

/// Binary Deserializer: reads BESQ domain types from a byte stream.
class Deserializer {
public:
    explicit Deserializer(const std::vector<uint8_t>& data)
        : _stream(data.data(), data.size()) {}

    Deserializer(const uint8_t* data, size_t size)
        : _stream(data, size) {}

    // ── Status ──
    bool ok()       const { return _stream.ok(); }
    bool has_more() const { return _stream.has_more(); }
    const uint8_t* pos() const { return _stream.pos(); }

    // ── Primitives ──
    uint8_t  u8()  { return _stream.u8(); }
    uint32_t u32() { return _stream.u32(); }
    int32_t  i32() { return _stream.i32(); }
    std::string string() { return _stream.string(); }

    // ── Domain types ──
    Ench read_ench() {
        int32_t id = _stream.i32();
        int32_t level = _stream.i32();
        if (!_stream.ok()) return Ench(0, 0);
        return Ench(id, level);
    }
    EnchSet read_ench_set() {
        EnchSet result;
        uint32_t count = _stream.u32();
        for (uint32_t i = 0; i < count; ++i) {
            Ench e = read_ench();
            if (!_stream.ok()) break;
            result.insert(e);
        }
        return result;
    }
    ItemStack read_item_stack() {
        std::string eq_id = _stream.string();
        EnchSet ench = read_ench_set();
        int32_t pp = _stream.i32();
        int32_t dur = _stream.i32();
        int32_t prio = _stream.i32();
        if (!_stream.ok()) return {};

        if (!eq_id.empty()) {
            int32_t eid = registries::equipment().get_id(eq_id);
            if (eid >= 0) {
                const Equipment& eq_ref = registries::equipment().get(eid);
                ItemStack item(eq_ref, ench, pp, dur);
                item.priority = prio;
                return item;
            }
        }
        ItemStack item(ench, pp);
        item.durability = dur;
        item.priority = prio;
        return item;
    }
    EnchSolution::EnchStep read_step() {
        ItemStack a = read_item_stack();
        ItemStack b = read_item_stack();
        int32_t cost = _stream.i32();
        int32_t exp = _stream.i32();
        if (!_stream.ok()) return {};
        return {a, b, cost, exp};
    }
    std::vector<int32_t> read_i32_vec() {
        uint32_t count = _stream.u32();
        std::vector<int32_t> vec;
        vec.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            vec.push_back(_stream.i32());
            if (!_stream.ok()) break;
        }
        return vec;
    }

private:
    ByteStreamReader _stream;
};
