#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace compact {

using MaskType = uint64_t;
inline const constexpr size_t MASK_ELEM_SIZE = 8ULL * sizeof(MaskType);

struct EnchInfo {
    uint16_t mul;                   // 经验乘数
    uint16_t max_lvl;               // 最大等级
    std::vector<MaskType> exc_mask; // 互斥附魔位掩码
    bool applicable;                // 是否适用目标装备类别

    bool is_conflict(const EnchInfo &other) const noexcept;
};

struct Ench {
    int16_t id;
    int16_t level;

    bool operator==(const Ench& o) const noexcept { return id == o.id && level == o.level; }
};

enum class ItemType : uint8_t {
    Book,
    Equip,
    Material,
};

/// Compact set of Ench stored as sorted vector<Ench>.
///
/// Invariant: elements are always sorted by id (ascending). This makes
/// comparison, hashing, and binary-search lookup O(N) or O(log N) with
/// cache-friendly contiguous storage.
class EnchSet {
public:
    using value_type = Ench;
    using iterator = std::vector<Ench>::iterator;
    using const_iterator = std::vector<Ench>::const_iterator;

    EnchSet() = default;

    // ── Iterators (inline) ──
    iterator       begin()       noexcept { return _enchs.begin(); }
    iterator       end()         noexcept { return _enchs.end(); }
    const_iterator begin() const noexcept { return _enchs.begin(); }
    const_iterator end()   const noexcept { return _enchs.end(); }

    // ── Capacity (inline) ──
    size_t size()    const noexcept { return _enchs.size(); }
    bool   empty()   const noexcept { return _enchs.empty(); }
    void   reserve(size_t n)        { _enchs.reserve(n); }

    // ── Lookup ──
    iterator       find(int16_t id)       noexcept;
    const_iterator find(int16_t id) const noexcept;
    bool contains(int16_t id) const noexcept;

    // ── Modifiers ──
    void insert(Ench ench);
    void merge(const EnchSet& other);
    void clear() { _enchs.clear(); }

    // ── Comparison (inline) ──
    bool operator==(const EnchSet& o) const noexcept { return _enchs == o._enchs; }
    bool operator!=(const EnchSet& o) const noexcept { return _enchs != o._enchs; }

private:
    std::vector<Ench> _enchs;
};

struct Item {
    ItemType type;                  // 物品类型
    int16_t dur;                    // 耐久度
    uint8_t ppn;                    // 前次惩罚次数
    EnchSet enchs;                  // 附魔列表（按 id 排序）

    bool operator==(const Item& o) const noexcept {
        return type == o.type && dur == o.dur && ppn == o.ppn
            && enchs == o.enchs;
    }
};

struct EnchStep {
    Item base;         // 锻造前的目标物品
    Item sacrifice;    // 锻造前的祭品
    int32_t cost;      // 经验等级消耗
};

} // namespace compact

template<>
struct std::hash<compact::Ench> {
    size_t operator()(const compact::Ench& e) const noexcept {
        return static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16);
    }
};

template<>
struct std::hash<compact::Item> {
    size_t operator()(const compact::Item& item) const noexcept {
        size_t h = static_cast<size_t>(item.type)
                 ^ (static_cast<size_t>(item.ppn) << 8)
                 ^ (static_cast<size_t>(item.dur) << 16);
        // Combine enchantment hashes
        for (const auto& e : item.enchs)
            h ^= std::hash<compact::Ench>{}(e) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
