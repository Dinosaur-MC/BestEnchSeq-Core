#include "EnchSet.h"

#include "EnchInfo.h"

void EnchSet::update_cache() const {
    _cache.incompatible.clear();
    for (auto &ench : *this) {
        for (auto &e : ench.get_exclusive_set())
            _cache.incompatible.emplace(e);
    }
}

const EnchSet::Cache &EnchSet::get_cache() const { return _cache; }

bool EnchSet::is_incompatible(const int32_t e) const {
    return _cache.incompatible.find(e) != _cache.incompatible.end();
}
bool EnchSet::is_incompatible_s(const int32_t e) const {
    update_cache();
    return _cache.incompatible.find(e) != _cache.incompatible.end();
}

EnchSet EnchSet::combine(const EnchSet &other) const {
    EnchSet result = *this;
    for (const Ench &e : other) {
        if (!is_incompatible(e.id)) {
            auto it = result.find(e);
            if (it != result.end()) {
                int32_t new_level = *it + e.level;
                result.erase(it);
                result.emplace(e.id, new_level);
            } else {
                result.emplace(e);
            }
        }
    }
    return result;
}
EnchSet EnchSet::combine_s(const EnchSet &other) const {
    update_cache();
    return combine(other);
}

int32_t EnchSet::combine(const EnchSet &other, bool is_book) {
    if (other.empty())
        return 0;
    bool need_update = false;
    int32_t result   = 0;
    platform::MCE type         = EnchInfo::get_active_platform();
    for (const Ench &e : other) {
        if (is_incompatible(e.id)) {
            // 有冲突，不合并
            result += type == platform::MCE::Java ? 2 : 1;
        } else {
            int32_t multiplier = e.get_multiplier(is_book);
            auto it            = this->find(e);
            if (it != this->end()) {
                int32_t old_level = it->level;
                int32_t new_level = *it + e.level;

                // 用新元素替换旧元素
                this->erase(it);
                this->emplace(e.id, new_level);

                if (multiplier > 0) {
                    if (type == platform::MCE::Java)
                        result += multiplier * new_level;
                    else
                        result += multiplier * (new_level - old_level);
                }
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
int32_t EnchSet::combine_s(const EnchSet &other, bool is_book) {
    update_cache();
    return combine(other, is_book);
}

std::pair<EnchSet, int32_t> EnchSet::combine(const EnchSet &other, bool is_book) const {
    if (other.empty())
        return {*this, 0};

    EnchSet ret_ench = *this;
    int32_t ret_cost = 0;
    platform::MCE type         = EnchInfo::get_active_platform();
    for (const Ench &e : other) {
        if (is_incompatible(e.id)) {
            // 有冲突，不合并
            ret_cost += type == platform::MCE::Java ? 2 : 1;
        } else {
            int32_t multiplier = e.get_multiplier(is_book);
            auto it            = ret_ench.find(e);
            if (it != ret_ench.end()) {
                int32_t old_level = it->level;
                int32_t new_level = *it + e.level;

                // 用新元素替换旧元素
                ret_ench.erase(it);
                ret_ench.emplace(e.id, new_level);

                if (multiplier > 0) {
                    if (type == platform::MCE::Java)
                        ret_cost += multiplier * new_level;
                    else
                        ret_cost += multiplier * (new_level - old_level);
                }
            } else {
                // 插入新元素
                ret_ench.emplace(e);

                if (multiplier > 0)
                    ret_cost += multiplier * e.level;
            }
        }
    }
    return {ret_ench, ret_cost};
}
std::pair<EnchSet, int32_t> EnchSet::combine_s(const EnchSet &other, bool is_book) const {
    update_cache();
    return combine(other, is_book);
}
