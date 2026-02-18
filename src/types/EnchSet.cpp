#include "EnchInfo.h"
#include "EnchSet.h"

void EnchSet::update_cache() const {
    cache.incompatible.clear();
    int32_t level_cost = 0;
    for (auto &ench : *this) {
        for (auto &e : ench.get_incompatible())
            cache.incompatible.emplace(e);
        level_cost += ench.get_current_multiplier() * ench.level;
    }
    cache.level_cost = level_cost;
}

const EnchSet::Cache &EnchSet::get_cache() const { return cache; }

bool EnchSet::is_incompatible(const int32_t e) const {
    return cache.incompatible.find(e) != cache.incompatible.end();
}
bool EnchSet::is_incompatible_s(const int32_t e) const {
    update_cache();
    return cache.incompatible.find(e) != cache.incompatible.end();
}

int32_t EnchSet::combine(const EnchSet &other) {
    if (other.empty())
        return 0;
    bool need_update = false;
    int32_t result   = 0;
    MCE type         = EnchInfo::get_active_platform();
    for (const Ench &e : other) {
        if (is_incompatible(e.id)) {
            // 有冲突，不合并
            result += type == MCE::Java ? 2 : 1;
        } else {
            int32_t multiplier = e.get_multiplier(type);
            auto it            = this->find(e);
            if (it != this->end()) {
                int32_t old_level = it->level;
                int32_t new_level = std::min(
                    e.get_max_level(), old_level == e.level ? e.level + 1 : std::max(old_level, e.level)
                );

                if (multiplier > 0) {
                    if (type == MCE::Java)
                        result += multiplier * new_level;
                    else
                        result += multiplier * (new_level - old_level);
                }

                // 用新元素替换旧元素
                this->erase(it);
                this->emplace(e.id, new_level);
            } else {
                // 插入新元素
                this->emplace(e);
                if (multiplier > 0)
                    result += multiplier * e.level;
            }
            need_update = true;
        }
    }
    if (need_update)
        update_cache();
    return result;
}
int32_t EnchSet::combine_s(const EnchSet &other) {
    update_cache();
    return combine(other);
}
