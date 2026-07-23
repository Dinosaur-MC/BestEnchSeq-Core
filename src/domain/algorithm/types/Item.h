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

    Item() = default;
    Item(ItemType type_, int16_t dur_, uint8_t ppn_, EnchSet enchs_) noexcept
        : type(type_), dur(dur_), ppn(ppn_), enchs(std::move(enchs_)) {}

    bool operator==(const Item &o) const noexcept {
        return type == o.type && dur == o.dur && ppn == o.ppn && enchs == o.enchs;
    }

    void serialize(ByteStreamWriter &w) const noexcept {
        w << static_cast<uint8_t>(type) << dur << ppn << enchs;
    }
    void deserialize(ByteStreamReader &r) noexcept {
        uint8_t t;
        r >> t >> dur >> ppn >> enchs;
        type = static_cast<ItemType>(t);
    }
};

using ItemCollection = std::vector<Item>;

// ── Free-function streaming for Item (non-virtual, ADL via algorithm ns) ──

inline ByteStreamWriter& operator<<(ByteStreamWriter& w, const Item& item) {
    item.serialize(w);
    return w;
}
inline ByteStreamReader& operator>>(ByteStreamReader& r, Item& item) {
    item.deserialize(r);
    return r;
}

// ── vector<Item> streaming (ItemCollection) ──

inline ByteStreamWriter& operator<<(ByteStreamWriter& w, const ItemCollection& items) {
    w << items.size();
    for (const auto& item : items)
        w << item;
    return w;
}
inline ByteStreamReader& operator>>(ByteStreamReader& r, ItemCollection& items) {
    size_t n{};
    r.read(n);
    if (!r.ok()) { items.clear(); return r; }
    items.resize(n);
    for (auto& item : items)
        r >> item;
    return r;
}

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
