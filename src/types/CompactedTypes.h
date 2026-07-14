#pragma once
#include <cstdint>
#include <vector>
#include "utils/HashUtils.hpp"

namespace compact {

using MaskType = uint64_t;
inline constexpr size_t MASK_ELEM_SIZE = 8ULL * sizeof(MaskType);

struct EnchInfo {
    uint16_t mul;                   // 经验乘数
    uint16_t mul_b;                 // 书本经验乘数
    uint16_t max_lvl;               // 最大等级
    std::vector<MaskType> exc_mask; // 互斥附魔位掩码
    bool applicable;                // 是否适用目标装备类别

    [[nodiscard]] bool is_conflict(const EnchInfo &other) const noexcept;
};

struct Ench {
    int16_t id;
    int16_t level;

    bool operator==(const Ench &o) const noexcept { return id == o.id && level == o.level; }
};

using EnchCollection = std::vector<Ench>;

/// Compact set of Ench with small-object optimization.
///
/// Stores up to 16 enchants inline (64 bytes, zero heap).  Eliminates
/// ~12M small allocations that the previous vector<Ench> approach incurred
/// across search hot paths.
///
/// Invariant: elements are always sorted by id (ascending).
class EnchSet {
  public:
    static constexpr size_t INLINE_N = 16;
    static constexpr size_t INLINE_BYTES = INLINE_N * sizeof(Ench);  // 64

    using value_type = Ench;
    using iterator = Ench*;
    using const_iterator = const Ench*;

    EnchSet() noexcept : _size(0) {}

    EnchSet(const EnchSet &o) noexcept : _size(o._size) {
        __builtin_memcpy(_buf, o._buf, INLINE_BYTES);
    }

    EnchSet &operator=(const EnchSet &o) noexcept {
        if (this != &o) { _size = o._size; __builtin_memcpy(_buf, o._buf, INLINE_BYTES); }
        return *this;
    }

    EnchSet(EnchSet &&o) noexcept : _size(o._size) {
        __builtin_memcpy(_buf, o._buf, INLINE_BYTES);
        o._size = 0;
    }

    EnchSet &operator=(EnchSet &&o) noexcept {
        if (this != &o) { _size = o._size; __builtin_memcpy(_buf, o._buf, INLINE_BYTES); o._size = 0; }
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
    void clear() noexcept { _size = 0; _hash_cache = 0; }
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
    [[nodiscard]] size_t hash() const noexcept {
        if (_hash_cache == 0 && _size > 0) {
            size_t h = _size;
            const Ench *d = reinterpret_cast<const Ench *>(_buf);
            for (size_t i = 0; i < _size; ++i)
                hash_combine(h, static_cast<size_t>(d[i].id) ^ (static_cast<size_t>(d[i].level) << 16));
            _hash_cache = h;
        }
        return _hash_cache;
    }

    /// Force-recompute the hash cache.  Use after raw buffer modifications
    /// via mutable iterators when insert/clear/sort are not an option.
    void rehash() const noexcept {
        _hash_cache = 0;
        (void)hash();
    }

    // ── Comparison ──
    bool operator==(const EnchSet &o) const noexcept {
        return _size == o._size &&
               __builtin_memcmp(_buf, o._buf, _size * sizeof(Ench)) == 0;
    }
    bool operator!=(const EnchSet &o) const noexcept { return !(*this == o); }

  private:
    uint8_t _size{0};
    mutable size_t _hash_cache{0};
    alignas(Ench) uint8_t _buf[INLINE_N * sizeof(Ench)];
};

enum class ItemType : uint8_t {
    Book,
    Equip,
    Material,
};

struct Item {
    ItemType type; // 物品类型
    int16_t dur;   // 耐久度
    uint8_t ppn;   // 前次惩罚次数
    EnchSet enchs; // 附魔列表（按 id 排序）

    bool operator==(const Item &o) const noexcept { return type == o.type && dur == o.dur && ppn == o.ppn && enchs == o.enchs; }
};

using ItemCollection = std::vector<Item>;

struct EnchStep {
    Item base;      // 锻造前的目标物品
    Item sacrifice; // 锻造前的祭品
    int32_t cost;   // 经验等级消耗
};

/// A complete solution: ordered forge steps + total cost.
struct EnchSolution {
    std::vector<EnchStep> steps;
    int32_t total_cost{0};
};

} // namespace compact

template <> struct std::hash<compact::Ench> {
    size_t operator()(const compact::Ench &e) const noexcept {
        return static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16);
    }
};

template <> struct std::hash<compact::Item> {
    size_t operator()(const compact::Item &item) const noexcept {
        size_t h = static_cast<size_t>(item.type);
        hash_combine(h, static_cast<size_t>(item.ppn));
        hash_combine(h, static_cast<size_t>(item.dur));
        hash_combine(h, item.enchs.hash());
        return h;
    }
};

template <> struct std::hash<compact::EnchSet> {
    size_t operator()(const compact::EnchSet &s) const noexcept { return s.hash(); }
};
