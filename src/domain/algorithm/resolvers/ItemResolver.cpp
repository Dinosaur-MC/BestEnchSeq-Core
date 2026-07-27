#include "ItemResolver.h"
#include <cstdint>

namespace algorithm {

ResolverOutput ItemResolver::resolve(const Item &target_item, const EnchSet &source_ench) {
    const EnchSet &target_ench = target_item.enchs;

    // Step 1: Compute diff = target_ench - source_ench
    EnchSet diff;
    for (const Ench &wanted : target_ench) {
        auto it = source_ench.find(wanted.id);
        if (it == source_ench.end()) {
            diff.insert(wanted);
        } else if (it->level < wanted.level) {
            int16_t level = it->level + 1 == wanted.level ? it->level : wanted.level;
            diff.insert(Ench{wanted.id, level});
        }
    }

    // If diff is empty, the target is already satisfied
    if (diff.empty())
        return {};

    // Step 2: Generate books for the diff.
    ResolverOutput books;
    for (const Ench &wanted : diff) {
        books.push_back(Item{ItemType::Book, 0, 0, {wanted}});
    }

    return books;
}

} // namespace algorithm
