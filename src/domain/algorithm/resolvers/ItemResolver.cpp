#include "ItemResolver.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace algorithm {

ResolvedInput ItemResolver::resolve(
    const Item& target_item, const EnchSet& source_ench, const EnchSet& target_ench)
{
    // Step 1: Compute diff = target_ench - source_ench
    // Only include enchantments where the desired level exceeds the existing level.
    EnchSet diff;
    for (const Ench& wanted : target_ench) {
        auto it = source_ench.find(wanted.id);
        int16_t existing_level = (it != source_ench.end()) ? it->level : 0;
        if (existing_level < wanted.level) {
            diff.insert(Ench{wanted.id, wanted.level});
        }
    }

    // Step 2: Generate graduated books for the diff.
    // For each enchantment in the diff, create individual books at every
    // required level from (existing + 1) up to the desired level.  Each
    // book holds exactly one enchant at its specific level.
    ItemCollection books;
    for (const Ench& wanted : diff) {
        auto it = source_ench.find(wanted.id);
        int16_t existing_level = (it != source_ench.end()) ? it->level : 0;
        for (int16_t lvl = existing_level + 1; lvl <= wanted.level; ++lvl) {
            EnchSet book_enchs;
            book_enchs.insert(Ench{wanted.id, lvl});
            books.push_back({ItemType::Book, 0, 0, std::move(book_enchs)});
        }
    }

    // The returned target_item must have only the SOURCE (current) enchantments,
    // not the desired ones.  The desired state goes into target_ench separately.
    // This ensures the algorithm doesn't see the target as already met.
    Item result_item = target_item;
    result_item.enchs = source_ench;
    return ResolvedInput{result_item, source_ench, target_ench, std::move(books)};
}

} // namespace algorithm
