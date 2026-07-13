#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
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
/// Stores up to 6 enchants inline (24 bytes, zero heap).  When the set
/// grows beyond INLINE_N elements it transparently switches to heap
/// storage.  This eliminates ~12M small allocations in search hot paths.
///
/// Invariant: elements are always sorted by id (ascending).
class EnchSet {
  public:
    static constexpr size_t INLINE_N = 6;

    using value_type = Ench;
    using iterator = Ench*;
    using const_iterator = const Ench*;

    // ── Constructors / destructor ──

    EnchSet() noexcept : _size(0), _mode(0) {}

    EnchSet(const EnchSet &o) : _size(0), _mode(0) { _copy_from(o); }

    EnchSet &operator=(const EnchSet &o) noexcept {
        if (this != &o) { _clear_data(); _copy_from(o); }
        return *this;
    }

    EnchSet(EnchSet &&o) noexcept : _size(o._size), _mode(o._mode) {
        if (o._is_inline()) {
            std::copy(o._buf, o._buf + o._size * sizeof(Ench), _buf);
        } else {
            new (&_vec) std::vector<Ench>(std::move(o._vec));
        }
        o._size = 0; o._mode = 0;
        o._vec.~vector();
        new (&o._vec) std::vector<Ench>();  // replaced
        (void)o._vec;  // ensure vec is in valid empty state
    }

    EnchSet &operator=(EnchSet &&o) noexcept {
        if (this != &o) {
            _clear_data();
            _size = o._size; _mode = o._mode;
            if (o._is_inline()) {
                std::copy(o._buf, o._buf + o._size * sizeof(Ench), _buf);
            } else {
                new (&_vec) std::vector<Ench>(std::move(o._vec));
            }
            o._size = 0; o._mode = 0;
            o._vec.~vector();
            new (&o._vec) std::vector<Ench>();
        }
        return *this;
    }

    ~EnchSet() noexcept { _clear_data(); }

    // ── Iterators ──
    iterator begin() noexcept { return _data(); }
    iterator end() noexcept { return _data() + _size; }
    const_iterator begin() const noexcept { return _data(); }
    const_iterator end() const noexcept { return _data() + _size; }

    // ── Capacity ──
    size_t size() const noexcept { return _size; }
    [[nodiscard]] bool empty() const noexcept { return _size == 0; }
    void reserve(size_t n) {
        if (n > INLINE_N && !_is_inline()) _vec.reserve(n);
    }

    // ── Lookup ──
    [[nodiscard]] iterator find(int16_t id) noexcept;
    [[nodiscard]] const_iterator find(int16_t id) const noexcept;
    [[nodiscard]] bool contains(int16_t id) const noexcept;

    // ── Modifiers ──
    void insert(const Ench &ench);
    void clear() noexcept { _clear_data(); _size = 0; _mode = 0; }

    /// Re-sort to restore the sorted-by-id invariant after external mutation.
    void sort();

    // ── Hash ──
    [[nodiscard]] size_t hash() const noexcept {
        size_t h = _size;
        for (size_t i = 0; i < _size; ++i) {
            auto &e = _data()[i];
            hash_combine(h, static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16));
        }
        return h;
    }

    // ── Comparison ──
    bool operator==(const EnchSet &o) const noexcept;
    bool operator!=(const EnchSet &o) const noexcept { return !(*this == o); }

  private:
    uint8_t _size : 7;       // element count (0-127)
    uint8_t _mode : 1;       // 0=inline, 1=heap

    // Inline storage (6 Ench × 4 bytes = 24 bytes)
    alignas(Ench) uint8_t _buf[INLINE_N * sizeof(Ench)];

    // Heap storage (used when mode=1)
    std::vector<Ench> _vec;

    bool _is_inline() const noexcept { return _mode == 0; }

    Ench *_data() noexcept { return _is_inline() ? reinterpret_cast<Ench *>(_buf) : _vec.data(); }
    const Ench *_data() const noexcept { return _is_inline() ? reinterpret_cast<const Ench *>(_buf) : _vec.data(); }

    void _clear_data() noexcept {
        if (!_is_inline()) _vec.~vector();
    }

    void _copy_from(const EnchSet &o) {
        _size = o._size;
        if (o._is_inline()) {
            _mode = 0;
            std::copy(o._buf, o._buf + _size * sizeof(Ench), _buf);
        } else {
            _mode = 1;
            new (&_vec) std::vector<Ench>(o._vec);
        }
    }

    /// Transition from inline to heap storage.
    void _migrate_to_heap(size_t new_cap);
};

// ─── EnchSet inline helpers ─────────────────────────────────────────────

inline bool EnchSet::operator==(const EnchSet &o) const noexcept {
    if (_size != o._size) return false;
    const Ench *a = _data(), *b = o._data();
    for (size_t i = 0; i < _size; ++i)
        if (a[i].id != b[i].id || a[i].level != b[i].level) return false;
    return true;
}

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
