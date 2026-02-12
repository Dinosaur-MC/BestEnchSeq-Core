#include "EnchInfo.h"
#include "EnchSet.h"
#include "common.h"

void EnchSet::update_cache() const {
    cache.incompatible.clear();
    int32_t level_cost = 0;
    for (auto &ench : *this) {
        for (auto &e : ench.get_incompatible()) {
            cache.incompatible.insert(e);
        }
        level_cost += ench.get_multiplier() * ench.lvl;
    }
    cache.level_cost = level_cost;
}

const EnchSet::Cache &EnchSet::get_cache() const { return cache; }

bool EnchSet::is_incompatible(const int32_t e) const {
    return cache.incompatible.find(e) != cache.incompatible.end();
}

int32_t EnchSet::combine(const EnchSet &other) {
    int32_t result = 0;
    if (other.empty())
        return 0;
    update_cache();
    for (const Ench &e : other) {
        if (is_incompatible(e.id)) {
            result += EnchInfo::get_active_platform() == MCE::Java ? 2 : 1;
        } else {
            auto it = this->find(e);
            if (it != this->end()) {
                // 元素已存在，更新lvl
                int32_t lvl =
                    std::min(e.get_max_level(), it->lvl == e.lvl ? e.lvl + 1 : std::max(it->lvl, e.lvl));
                int32_t multiplier = e.get_multiplier();

                // 计算增加值
                if (multiplier > 0) {
                    if (EnchInfo::get_active_platform() == MCE::Java) {
                        result += multiplier * lvl;
                    } else {
                        result += multiplier * (lvl - it->lvl);
                    }
                }

                it->lvl = lvl;
            } else {
                // 插入新元素
                this->insert(e);
                result += e.get_multiplier() * e.lvl;
            }
        }
    }
    update_cache();
    return result;
}
