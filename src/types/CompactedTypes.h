#pragma once
#include <cstdint>
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
};

enum class ItemType : uint8_t {
    Book,
    Equip,
    Material,
};

struct Item {
    ItemType type;                  // 物品类型
    int16_t dur;                    // 耐久度
    uint8_t ppn;                     // 前次惩罚次数
    uint16_t lsum;                   // 经验等级总和
    std::vector<MaskType> exc_mask; // 互斥附魔位掩码
    std::vector<Ench> enchs;        // 附魔列表
};

} // namespace compact
