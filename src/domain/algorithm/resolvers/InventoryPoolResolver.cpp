#include "domain/algorithm/resolvers/InventoryPoolResolver.h"
#include "common/utils/bit_iterator.hpp"
#include "domain/algorithm/resolvers/DefaultResolver.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace algorithm {

namespace {

/// Usefulness of an extra pool item: value it carries toward the target
/// (capped at the target level, weighted ×4), minus what its non-target
/// enchantments and prior-work penalty would add to every forge it joins.
int64_t pool_score(const Item& it, const Item& target, const EnchReg& reg) {
    int64_t s = 0;
    bit_iterator<EnchSet::mask_type, uint8_t> bits(it.enchs.get_mask());
    for (auto id = bits.next(); id != bits.npos; id = bits.next()) {
        const int64_t mul = (it.type == ItemType::Book) ? reg[id].mul_b : reg[id].mul;
        const int64_t lvl = it.enchs[id];
        if (target.enchs.contains(id))
            s += 4 * std::min<int64_t>(lvl, target.enchs[id]) * mul;
        else
            s -= lvl * mul;
    }
    const int64_t pen = (it.ppn > 30) ? INT32_MAX : (int64_t{1} << it.ppn) - 1;
    return s - 2 * pen;
}

bool relevant(const Item& it, const Item& target) {
    if (it.type == ItemType::Equip)
        return target.type == ItemType::Equip; // base or sacrifice candidate
    if (it.type == ItemType::Book)
        return (it.enchs.get_mask() & target.enchs.get_mask()) != 0;
    return false;
}

} // namespace

ResolverOutput InventoryPoolResolver::resolve(const AlgorithmInput& input) const {
    ResolverOutput sel = DefaultResolver{}.resolve(input);
    if (!input.is_inventory() || sel.empty() || sel.size() >= kPoolMax)
        return sel;

    const auto* inv = std::get_if<InventoryPayload>(&input.data);
    if (!inv)
        return sel;

    // Pool items not already selected (multiset difference: identical books
    // may appear several times in the inventory).
    std::vector<char> used(sel.size(), 0);
    struct Cand {
        size_t index;
        int64_t score;
        int32_t priority;
    };
    std::vector<Cand> cands;
    for (size_t i = 0; i < inv->available.size(); ++i) {
        const Item& it = inv->available[i];
        bool in_sel = false;
        for (size_t s = 0; s < sel.size(); ++s)
            if (!used[s] && sel[s] == it) {
                used[s] = 1;
                in_sel = true;
                break;
            }
        if (in_sel || !relevant(it, input.target))
            continue;
        const int32_t prio = (i < inv->priorities.size()) ? inv->priorities[i] : 99;
        cands.push_back({i, pool_score(it, input.target, input.registry), prio});
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.priority < b.priority;
    });

    ResolverOutput out = std::move(sel); // selection first: out[0] stays the chosen base
    for (const Cand& c : cands) {
        if (out.size() >= kPoolMax)
            break;
        out.push_back(inv->available[c.index]);
    }
    return out;
}

} // namespace algorithm
