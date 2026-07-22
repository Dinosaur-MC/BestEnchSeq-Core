#pragma once
#include "Enchantment.h"

namespace algorithm {
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

    bool operator==(const Item &o) const noexcept {
        return type == o.type && dur == o.dur && ppn == o.ppn && enchs == o.enchs;
    }
};

using ItemCollection = std::vector<Item>;

} // namespace algorithm
template <> struct std::hash<algorithm::Item> {
    size_t operator()(const algorithm::Item &item) const noexcept {
        size_t h = static_cast<size_t>(item.type);
        hash_combine(h, static_cast<size_t>(item.ppn));
        hash_combine(h, static_cast<size_t>(item.dur));
        hash_combine(h, item.enchs.hash());
        return h;
    }
};
