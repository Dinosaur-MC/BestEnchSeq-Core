#pragma once
#include "common/CommonTypes.h"
#include <string>

struct EquipmentTag {
    NSID id;
    std::string name;

    bool operator==(const EquipmentTag &o) const { return id == o.id; }
    bool operator<(const EquipmentTag &o) const { return id.str() < o.id.str(); }

    // Builtin tag accessors — function-local static, avoids static init order
    static const NSID &dummy()       { static const NSID id("#minecraft:dummy");       return id; }
    static const NSID &sword()       { static const NSID id("#minecraft:sword");       return id; }
    static const NSID &helmet()      { static const NSID id("#minecraft:helmet");      return id; }
    static const NSID &chestplate()  { static const NSID id("#minecraft:chestplate");  return id; }
    static const NSID &leggings()    { static const NSID id("#minecraft:leggings");    return id; }
    static const NSID &boots()       { static const NSID id("#minecraft:boots");       return id; }
    static const NSID &pickaxe()     { static const NSID id("#minecraft:pickaxe");     return id; }
    static const NSID &axe()         { static const NSID id("#minecraft:axe");         return id; }
    static const NSID &shovel()      { static const NSID id("#minecraft:shovel");      return id; }
    static const NSID &hoe()         { static const NSID id("#minecraft:hoe");         return id; }
    static const NSID &bow()         { static const NSID id("#minecraft:bow");         return id; }
    static const NSID &crossbow()    { static const NSID id("#minecraft:crossbow");    return id; }
    static const NSID &trident()     { static const NSID id("#minecraft:trident");     return id; }
    static const NSID &shield()      { static const NSID id("#minecraft:shield");      return id; }
    static const NSID &fishing_rod() { static const NSID id("#minecraft:fishing_rod"); return id; }
};
