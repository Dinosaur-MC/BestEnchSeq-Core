#include "IAlgorithm.h"
#include "domain/algorithm/resolvers/DefaultResolver.h"
#include "domain/algorithm/types/AlgorithmTypes.h"

namespace algorithm {

std::unique_ptr<IResolver> IAlgorithm::get_resolver() const noexcept {
    return std::make_unique<DefaultResolver>();
}

bool IAlgorithm::simulate(const AlgorithmInput &input) const noexcept {
    switch (input.config.mode) {
    case AlgorithmMode::direct: {
        const auto *d = std::get_if<DirectPayload>(&input.data);
        if (!d)
            return false;
        Item base = input.target;
        base.enchs.clear();
        for (const Ench &e : d->source)
            base.enchs.insert(e);
        for (const auto &t : input.target.enchs) {
            if (base.enchs[t.id()] < t.level())
                return true;  // diff non-empty → books generatable
        }
        return false;  // target already met → no output
    }
    case AlgorithmMode::inventory: {
        const auto *inv = std::get_if<InventoryPayload>(&input.data);
        if (!inv || inv->available.empty())
            return false;
        if (input.target.type == ItemType::Equip) {
            bool has_book = false;
            for (const auto &it : inv->available) {
                if (it.type == ItemType::Book && !it.enchs.empty()) {
                    has_book = true;
                    break;
                }
            }
            if (!has_book)
                return false;  // no sacrifice book for an equipment target
        }
        return true;
    }
    }
    return false;
}

} // namespace algorithm
