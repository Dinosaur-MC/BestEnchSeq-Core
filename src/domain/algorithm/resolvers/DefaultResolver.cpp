#include "domain/algorithm/resolvers/DefaultResolver.h"
#include "domain/algorithm/resolvers/ItemResolver.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include <algorithm>
#include <vector>

namespace algorithm {

ResolverOutput DefaultResolver::resolve(const AlgorithmInput &input) const {
    switch (input.config.mode) {
    case AlgorithmMode::direct: {
        const auto *d = std::get_if<DirectPayload>(&input.data);
        if (!d)
            return {};
        // Base equipment = target with the source (current) enchantments.
        Item base = input.target;
        base.enchs.clear();
        for (const Ench &e : d->source)
            base.enchs.insert(e);
        // Generate the books needed to reach the target from the source.
        ResolverOutput books = ItemResolver::resolve(input.target, base.enchs);
        ResolverOutput out;
        out.reserve(books.size() + 1);
        out.push_back(std::move(base));  // always present → GoalAlreadyMet path
        for (auto &b : books)
            out.push_back(std::move(b));
        return out;
    }
    case AlgorithmMode::inventory: {
        const auto *inv = std::get_if<InventoryPayload>(&input.data);
        if (!inv || inv->available.empty())
            return {};
        // Stable priority sort, lower first (mirrors the former
        // InventoryResolver).  No equipment-first guarantee — the strategy
        // selects its own base equipment via Item::type.
        struct RankedItem {
            const Item *item;
            int32_t priority;
        };
        std::vector<RankedItem> ranked;
        ranked.reserve(inv->available.size());
        for (size_t i = 0; i < inv->available.size(); ++i) {
            int32_t prio = (i < inv->priorities.size()) ? inv->priorities[i] : 99;
            ranked.push_back({&inv->available[i], prio});
        }
        std::stable_sort(ranked.begin(), ranked.end(),
            [](const RankedItem &a, const RankedItem &b) {
                return a.priority < b.priority;
            });

        ResolverOutput out;
        out.reserve(ranked.size());
        for (const auto &r : ranked)
            out.push_back(*r.item);

        // Feasibility: an equipment target needs at least one non-empty book.
        if (input.target.type == ItemType::Equip) {
            bool has_book = false;
            for (const auto &item : out) {
                if (item.type == ItemType::Book && !item.enchs.empty()) {
                    has_book = true;
                    break;
                }
            }
            if (!has_book)
                return {};
        }
        return out;
    }
    }
    return {};
}

} // namespace algorithm
