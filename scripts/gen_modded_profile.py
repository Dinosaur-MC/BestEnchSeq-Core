#!/usr/bin/env python3
"""
Generate modded_sword.json — a profile that ONLY contains the extra
mod-style enchantments (no vanilla duplicates).  At runtime the benchmark
merges this profile with the vanilla profile to produce the full registry.
"""

import json, os

MOD_ENCHANTS = [
    {"id": "leeching", "name": "Leeching", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 4,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "brutality", "name": "Brutality", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 3,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "dexterity", "name": "Dexterity", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 2,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "gnashing", "name": "Gnashing", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 4,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "shattering", "name": "Shattering", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 3,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "subjugation", "name": "Subjugation", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 4,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "thunderbolting", "name": "Thunderbolting", "platform": "java",
     "max_level": 1, "limited_level": 1, "multiplier": 6,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "poison_aspect", "name": "Poison Aspect", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 3,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "wither_aspect", "name": "Wither Aspect", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 4,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "swift_slash", "name": "Swift Slash", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 2,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "velocity", "name": "Velocity", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 2,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "explosive", "name": "Explosive", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 5,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "windshear", "name": "Windshear", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 3,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
    {"id": "immolation", "name": "Immolation", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 3,
     "exclusive_set": [], "supported_items": ["#minecraft:swords"], "is_treasure": True},
]

profile = {
    "name": "modded_sword",
    "description": "Modded sword enchantments (incremental, merge with vanilla)",
    "author": "benchmark",
    "version": "1.0.0",
    "schema_version": "2.1.0",
    "enchantments": MOD_ENCHANTS,
    "equipments": [],
    "tags": [],
    "categories": []
}

out = os.path.join(os.path.dirname(__file__), "..", "data", "tests", "profiles", "modded_sword.json")
with open(out, "w") as f:
    json.dump(profile, f, indent=2, ensure_ascii=False)
print(f"Wrote: {out} ({len(MOD_ENCHANTS)} incremental enchants)")
