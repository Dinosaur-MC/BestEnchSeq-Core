#include "Item.h"

#include <stdexcept>

Item::Item(NSID id_, const EnchSet &enchs_, int32_t ppn_, int32_t dur_)
    : id(std::move(id_)), enchantments(enchs_), prior_penalty(ppn_), durability(dur_) {
    if (ppn_ < 0 || dur_ < 0)
        throw std::invalid_argument("Negative prior penalty or durability");
}

Item::Item(NSID id_, const EnchSet &enchs_, int32_t ppn_)
    : id(std::move(id_)), enchantments(enchs_), prior_penalty(ppn_), durability(0) {
    if (ppn_ < 0)
        throw std::invalid_argument("Negative prior penalty");
}

Json Item::to_json() const {
    return Json::object()
        .set("id", id.str())
        .set("enchantments", enchantments.to_json())
        .set("prior_penalty", prior_penalty)
        .set("durability", durability);
}

void Item::from_json(const Json& json) {
    if (json.has("id"))
        id = NSID(json["id"].as<std::string>());
    if (json.has("enchantments"))
        enchantments.from_json(json["enchantments"]);
    if (json.has("prior_penalty"))
        prior_penalty = static_cast<int32_t>(json["prior_penalty"].as<int64_t>());
    if (json.has("durability"))
        durability = static_cast<int32_t>(json["durability"].as<int64_t>());
}
