#include "Item.h"

#include <stdexcept>

Item::Item(NSID id_, const EnchSet& enchs_, int32_t ppn_, int32_t dur_)
    : id(std::move(id_)), enchantments(enchs_), prior_penalty(ppn_), durability(dur_) {
    if (ppn_ < 0 || dur_ < 0)
        throw std::invalid_argument("Negative prior penalty or durability");
}

Item::Item(NSID id_, const EnchSet& enchs_, int32_t ppn_)
    : id(std::move(id_)), enchantments(enchs_), prior_penalty(ppn_), durability(0) {
    if (ppn_ < 0)
        throw std::invalid_argument("Negative prior penalty");
}
