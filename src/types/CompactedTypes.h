#pragma once
#include <cstdint>
#include <functional>
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

/// Compact set of Ench — sorted vector<Ench> by default.
///
/// When BESQ_USE_ENCHSET_BITMAP is defined, uses a uint64_t bitmask for
/// O(1) contains() and a parallel sorted vector for iteration/lookup.
/// The bitmask eliminates 12M+ small heap allocations in search hot paths
/// (callgrind hotspot #10: vector<Ench>::_M_allocate).
///
/// Invariant: elements are always sorted by id (ascending). This makes
/// comparison, hashing, and binary-search lookup O(N) or O(log N) with
/// cache-friendly contiguous storage.
///
/// ⚠️ Mutable iterators (begin/end) expose the internal storage. Modifying
///    an element's id through them breaks the sorted invariant, causing
///    undefined behaviour in find/contains/insert. Use sort() to restore
///    the invariant if the underlying storage is mutated externally.
class EnchSet {
  public:
    using value_type = Ench;
    using iterator = std::vector<Ench>::iterator;
    using const_iterator = std::vector<Ench>::const_iterator;

    EnchSet() = default;

    // ── Iterators (inline) ──
    /// Mutable iterators — modifying element ids breaks the sorted invariant.
    /// Call sort() to restore after external mutation.
    iterator begin() noexcept { return _levels.begin(); }
    iterator end() noexcept { return _levels.end(); }
    const_iterator begin() const noexcept { return _levels.begin(); }
    const_iterator end() const noexcept { return _levels.end(); }

    // ── Capacity (inline) ──
    size_t size() const noexcept { return _levels.size(); }
    [[nodiscard]] bool empty() const noexcept { return _levels.empty(); }
    void reserve(size_t n) { _levels.reserve(n); }

    // ── Lookup ──
    [[nodiscard]] iterator find(int16_t id) noexcept;
    [[nodiscard]] const_iterator find(int16_t id) const noexcept;
    [[nodiscard]] bool contains(int16_t id) const noexcept;

    // ── Modifiers ──
    void insert(const Ench &ench);
    void clear() noexcept {
        _levels.clear();
#ifdef BESQ_USE_ENCHSET_BITMAP
        _present.clear();
#endif
    }

    /// Re-sort to restore the sorted-by-id invariant after external mutation
    /// through mutable iterators.
    void sort();

    // ── Hash (inline, avoids std::hash<Ench> before its specialization) ──
    [[nodiscard]] size_t hash() const noexcept {
        size_t h = _levels.size();
        for (const auto &e : _levels) {
            const size_t ench_hash = static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16);
            hash_combine(h, ench_hash);
        }
        return h;
    }

    // ── Comparison (inline) ──
    bool operator==(const EnchSet &o) const noexcept {
#ifdef BESQ_USE_ENCHSET_BITMAP
        if (_present != o._present) return false;
#endif
        return _levels == o._levels;
    }
    bool operator!=(const EnchSet &o) const noexcept { return !(*this == o); }

  private:
    std::vector<Ench> _levels;
#ifdef BESQ_USE_ENCHSET_BITMAP
    std::vector<uint64_t> _present;

    void _grow_for(int16_t id) {
        auto needed = static_cast<size_t>(id) / 64 + 1;
        if (needed > _present.size())
            _present.resize(needed, 0);
    }
#endif
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
