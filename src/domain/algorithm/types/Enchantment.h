#pragma once
#include "common/serialization/IBinarySerializable.h"
#include "utils/HashUtils.hpp"
#include <cstdint>
#include <cstring>
#include <vector>

namespace algorithm {

using MaskType                         = uint64_t;
inline constexpr size_t MASK_ELEM_SIZE = 8ULL * sizeof(MaskType);

struct EnchInfo : IBinarySerializable {
    uint16_t mul;                   // 经验乘数
    uint16_t mul_b;                 // 书本经验乘数
    uint16_t max_lvl;               // 最大等级
    std::vector<MaskType> exc_mask; // 互斥附魔位掩码
    bool applicable;                // 是否适用目标装备类别

    [[nodiscard]] bool is_conflict(const EnchInfo &other) const noexcept;

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << mul << mul_b << max_lvl << exc_mask << static_cast<uint8_t>(applicable);
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        uint8_t app;
        r >> mul >> mul_b >> max_lvl >> exc_mask >> app;
        applicable = app != 0;
    }
};

struct Ench {
    int16_t id;
    int16_t level;

    Ench() = default;
    Ench(int16_t id_, int16_t level_) noexcept : id(id_), level(level_) {}

    bool operator==(const Ench &o) const noexcept { return id == o.id && level == o.level; }

    void serialize(ByteStreamWriter &w) const noexcept { w << id << level; }
    void deserialize(ByteStreamReader &r) noexcept { r >> id >> level; }
};
static_assert(std::has_unique_object_representations_v<Ench>);

using EnchCollection = std::vector<Ench>;

// ── Free-function streaming for Ench (non-virtual, small value type) ──

inline ByteStreamWriter &operator<<(ByteStreamWriter &w, const Ench &e) {
    e.serialize(w);
    return w;
}
inline ByteStreamReader &operator>>(ByteStreamReader &r, Ench &e) {
    e.deserialize(r);
    return r;
}

/// Compact set of Ench with small-object optimization.
///
/// Stores up to 16 enchants inline (64 bytes, zero heap).  Eliminates
/// ~12M small allocations that the previous vector<Ench> approach incurred
/// across search hot paths.
///
/// Invariant: elements are always sorted by id (ascending).
class EnchSet {
  public:
    static constexpr size_t INLINE_N     = 16;
    static constexpr size_t INLINE_BYTES = INLINE_N * sizeof(Ench); // 64

    using value_type     = Ench;
    using iterator       = Ench *;
    using const_iterator = const Ench *;

    EnchSet() noexcept : _size(0) {}
    EnchSet(std::initializer_list<Ench> il) noexcept : _size(il.size()) {
        std::memcpy(_buf, il.begin(), sizeof(Ench) * il.size());
        sort();
    }
    template <
        typename Iter,
        std::enable_if_t<std::is_convertible_v<decltype(*std::declval<Iter &>()), Ench>, int> = 0>
    EnchSet(Iter first, Iter last) noexcept : _size(std::distance(first, last)) {
        std::copy(first, last, _buf);
        sort();
    }

    EnchSet(const EnchSet &o) noexcept : _size(o._size) { std::memcpy(_buf, o._buf, INLINE_BYTES); }

    EnchSet &operator=(const EnchSet &o) noexcept {
        if (this != &o) {
            _size = o._size;
            std::memcpy(_buf, o._buf, INLINE_BYTES);
        }
        return *this;
    }

    EnchSet(EnchSet &&o) noexcept : _size(o._size) {
        std::memcpy(_buf, o._buf, INLINE_BYTES);
        o._size = 0;
    }

    EnchSet &operator=(EnchSet &&o) noexcept {
        if (this != &o) {
            _size = o._size;
            std::memcpy(_buf, o._buf, INLINE_BYTES);
            o._size       = 0;
            o._hash_cache = 0;
        }
        return *this;
    }

    ~EnchSet() noexcept = default;

    // ── Iterators ──
    iterator begin() noexcept { return reinterpret_cast<Ench *>(_buf); }
    iterator end() noexcept { return reinterpret_cast<Ench *>(_buf) + _size; }
    const_iterator begin() const noexcept { return reinterpret_cast<const Ench *>(_buf); }
    const_iterator end() const noexcept { return reinterpret_cast<const Ench *>(_buf) + _size; }

    // ── Capacity ──
    size_t size() const noexcept { return _size; }
    [[nodiscard]] bool empty() const noexcept { return _size == 0; }
    void reserve(size_t) noexcept {}

    // ── Lookup ──
    [[nodiscard]] iterator find(int16_t id) noexcept;
    [[nodiscard]] const_iterator find(int16_t id) const noexcept;
    [[nodiscard]] bool contains(int16_t id) const noexcept;

    // ── Modifiers ──
    void insert(const Ench &ench);
    void clear() noexcept {
        _size       = 0;
        _hash_cache = 0;
    }
    void sort();

    // ── Hash (lazily cached) ──
    //
    // hash() caches the result on first call.  Insert / clear / sort
    // invalidate the cache automatically.
    //
    // ⚠  Mutable iterators (begin/end) grant raw write access to the
    //     inline buffer.  If you modify enchantments through them, you
    //     MUST call rehash() afterwards, otherwise hash() returns a
    //     stale value.  Prefer insert() / clear() / sort() when possible.
    [[nodiscard]] size_t hash() const noexcept;

    /// Force-recompute the hash cache.  Use after raw buffer modifications
    /// via mutable iterators when insert/clear/sort are not an option.
    void rehash() const noexcept {
        _hash_cache = 0;
        (void)hash();
    }

    // ── Comparison ──
    bool operator==(const EnchSet &o) const noexcept {
        return _size == o._size && std::memcmp(_buf, o._buf, _size * sizeof(Ench)) == 0;
    }
    bool operator!=(const EnchSet &o) const noexcept { return !(*this == o); }

    // ── Serialization (non-virtual; value-type) ──
    void serialize(ByteStreamWriter &w) const noexcept;
    void deserialize(ByteStreamReader &r) noexcept;

  private:
    uint8_t _size{0};
    mutable size_t _hash_cache{0};
    alignas(Ench) uint8_t _buf[INLINE_N * sizeof(Ench)];
};

// ── Free-function streaming for EnchSet (ADL via algorithm namespace) ──

inline ByteStreamWriter &operator<<(ByteStreamWriter &w, const EnchSet &s) {
    s.serialize(w);
    return w;
}
inline ByteStreamReader &operator>>(ByteStreamReader &r, EnchSet &s) {
    s.deserialize(r);
    return r;
}

} // namespace algorithm

template <> struct std::hash<algorithm::Ench> {
    size_t operator()(const algorithm::Ench &e) const noexcept {
        return static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16);
    }
};

template <> struct std::hash<algorithm::EnchSet> {
    size_t operator()(const algorithm::EnchSet &s) const noexcept { return s.hash(); }
};

template <> struct std::hash<algorithm::EnchCollection> {
    size_t operator()(const algorithm::EnchCollection &enchs) const noexcept {
        size_t h = enchs.size();
        for (const auto &ench : enchs)
            hash_combine(h, std::hash<algorithm::Ench>()(ench));
        return h;
    }
};
