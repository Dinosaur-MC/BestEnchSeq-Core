#pragma once
#include "common/CommonTypes.h"
#include "common/i18n/Language.h"
#include <string>

/// Display name for an enchantment from its NSID + active Language.
///
/// Looks up ``tr("enchantment.{ns}.{id}")`` — e.g. ``tr("enchantment.minecraft.sharpness")``.
/// Falls back to ``id.str()`` (``"minecraft:sharpness"``) if no translation found.
inline std::string ench_display_name(const NSID& id) {
    auto key = id.str([](std::string_view ns, std::string_view id) {
        return std::string("enchantment.") + std::string(ns) + "." + std::string(id);
    });
    auto name = tr(key);
    return name != key ? std::move(name) : std::string(id.str());
}

/// Display name for an item/equipment from its NSID + active Language.
///
/// Looks up ``tr("item.{ns}.{id}")`` — e.g. ``tr("item.minecraft.diamond_sword")``.
/// Falls back to ``id.str()`` (``"minecraft:diamond_sword"``) if no translation found.
inline std::string item_display_name(const NSID& id) {
    auto key = id.str([](std::string_view ns, std::string_view id) {
        return std::string("item.") + std::string(ns) + "." + std::string(id);
    });
    auto name = tr(key);
    return name != key ? std::move(name) : std::string(id.str());
}
