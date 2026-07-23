#include "ItemResolver.h"
#include <cstdint>

namespace algorithm {

ResolverOutput ItemResolver::resolve(
    const Item& target_item, const EnchSet& source_ench)
{
    const EnchSet& target_ench = target_item.enchs;

    // Step 1: Compute diff = target_ench - source_ench
    EnchSet diff;
    for (const Ench& wanted : target_ench) {
        auto it = source_ench.find(wanted.id);
        int16_t existing_level = (it != source_ench.end()) ? it->level : 0;
        if (existing_level < wanted.level) {
            diff.insert(Ench{wanted.id, wanted.level});
        }
    }

    // If diff is empty, the target is already satisfied
    if (diff.empty())
        return {};

    // Step 2: Generate graduated books for the diff.
    ResolverOutput books;
    for (const Ench& wanted : diff) {
        auto it = source_ench.find(wanted.id);
        int16_t existing_level = (it != source_ench.end()) ? it->level : 0;
        for (int16_t lvl = existing_level + 1; lvl <= wanted.level; ++lvl) {
            EnchSet book_enchs;
            book_enchs.insert(Ench{wanted.id, lvl});
            books.push_back({ItemType::Book, 0, 0, std::move(book_enchs)});
        }
    }

    return books;
}

} // namespace algorithm
