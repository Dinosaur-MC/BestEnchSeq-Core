#include "ItemResolver.h"

namespace algorithm {

ResolverOutput ItemResolver::resolve(const Item &target_item, const EnchSet &source_ench) {
    const EnchSet &target_ench = target_item.enchs;

    // Step 1: Compute diff = target_ench - source_ench
    EnchSet diff;
    for (const auto &wanted : target_ench) {
        auto id = wanted.id();
        if (!source_ench.contains(id)) {
            diff.insert(Ench{id, wanted.level()});
        } else if (source_ench[id] < wanted.level()) {
            auto lvl = static_cast<Ench::value_type>(
                source_ench[id] + 1 == wanted.level() ? source_ench[id] : wanted.level());
            diff.insert(Ench{id, lvl});
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
