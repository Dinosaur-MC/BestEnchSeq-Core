#include "algorithm.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

/* ====================
 *        N类实现
 * ==================== */

bool N::operator==(const N &other) const { return id == other.id; }

// 静态方法实现
void N::initialize_matches(const std::vector<std::pair<int32_t, int32_t>> &matches) {
    for (const auto &pair : matches) {
        int32_t a = pair.first;
        int32_t b = pair.second;
        if (a == b)
            continue;
        match_table[a].insert(b);
        match_table[b].insert(a);
    }
}

const std::unordered_set<int32_t> &N::get_matches(int32_t n) {
    static const std::unordered_set<int32_t> empty_set;
    auto it = match_table.find(n);
    return it != match_table.end() ? it->second : empty_set;
}

bool N::is_match(int32_t n1, int32_t n2) {
    if (n1 == n2)
        return false;
    const auto &matches = get_matches(n1);
    return matches.find(n2) != matches.end();
}

// 成员方法
const std::unordered_set<int32_t> &N::get_matches() const { return get_matches(id); }

bool N::is_match(const N &other) const { return is_match(id, other.id); }

// 构造函数
N::N(int32_t id) : id(id), lvl(1) {}
N::N(int32_t id, int32_t lvl) : id(id), lvl(lvl) {}

// N静态成员定义
std::unordered_map<int32_t, std::unordered_set<int32_t>> N::match_table;

/* ====================
 *      NSet类实现
 * ==================== */

void NSet::update_matches() {
    matches.clear();
    for (const auto &n : elements) {
        const auto &n_matches = n.get_matches();
        matches.insert(n_matches.begin(), n_matches.end());
    }
}

void NSet::insert(const N &n) {
    // 如果元素已存在，更新lvl
    auto it = elements.find(n);
    if (it != elements.end()) {
        // 元素已存在，更新lvl
        it->lvl = std::max(it->lvl, n.lvl);
    } else {
        // 插入新元素
        elements.insert(n);
    }
    update_matches();
}

void NSet::remove(int32_t id) {
    N dummy(id);
    auto it = elements.find(dummy);
    if (it != elements.end()) {
        elements.erase(it);
        update_matches();
    }
}

void NSet::clear() {
    elements.clear();
    matches.clear();
}

size_t NSet::size() const { return elements.size(); }

bool NSet::empty() const { return elements.empty(); }

const N *NSet::find(int32_t id) const {
    N dummy(id);
    auto it = elements.find(dummy);
    return it != elements.end() ? &(*it) : nullptr;
}

const std::unordered_set<N, N::Hash> &NSet::get_elements() const { return elements; }

const std::unordered_set<int32_t> &NSet::get_matches() const { return matches; }

bool NSet::is_match(const N &other) const { return matches.find(other.id) != matches.end(); }

// 计算b中与当前集合匹配的元素数量
int NSet::match_count(const NSet &b) const {
    if (this->empty() || b.empty())
        return 0;

    int count = 0;
    for (const auto &n : b.get_elements()) {
        if (this->is_match(n)) {
            count++;
        }
    }
    return count;
}

// 合并两个NSet并计算合并过程产生的"value"
int32_t NSet::combine(const NSet &other) {
    int32_t result = 0;
    for (const N &n : other.elements) {
        if (is_match(n)) {
            result += NInfo::get_active_type() == NInfo::Type::A ? 2 : 1;
        } else {
            auto it = elements.find(n);
            if (it != elements.end()) {
                // 元素已存在，更新lvl
                int32_t lvl =
                    std::min(n.get_max_lvl(), it->lvl == n.lvl ? n.lvl + 1 : std::max(it->lvl, n.lvl));
                result +=
                    n.get_multiplier() * (NInfo::get_active_type() == NInfo::Type::A ? lvl : lvl - it->lvl);
                it->lvl = lvl;
            } else {
                // 插入新元素
                elements.insert(n);
                result += n.get_multiplier() * n.lvl;
            }
        }
    }
    update_matches(); // 更新匹配集合
    return result;
}

NSet::NSet(const std::initializer_list<N> &elems) : elements(elems) { update_matches(); }

// 全局match_count函数（两个NSet之间的匹配计数）
int match_count(const NSet &a, const NSet &b) { return a.match_count(b); }
