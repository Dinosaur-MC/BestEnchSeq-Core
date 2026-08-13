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
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "brutality", "name": "Brutality", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 3,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "dexterity", "name": "Dexterity", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 2,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "gnashing", "name": "Gnashing", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 4,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "shattering", "name": "Shattering", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 3,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "subjugation", "name": "Subjugation", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 4,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "thunderbolting", "name": "Thunderbolting", "platform": "java",
     "max_level": 1, "limited_level": 1, "multiplier": 6,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "poison_aspect", "name": "Poison Aspect", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 3,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "wither_aspect", "name": "Wither Aspect", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 4,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "swift_slash", "name": "Swift Slash", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 2,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "velocity", "name": "Velocity", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 2,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "explosive", "name": "Explosive", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 5,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "windshear", "name": "Windshear", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 3,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "immolation", "name": "Immolation", "platform": "java",
     "max_level": 2, "limited_level": 2, "multiplier": 3,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "attack_speed", "name": "Attack Speed", "platform": "java",
     "max_level": 3, "limited_level": 3, "multiplier": 3,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "certainty", "name": "Certainty", "platform": "java",
     "max_level": 5, "limited_level": 5, "multiplier": 2,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
    {"id": "divinity", "name": "Divinity", "platform": "java",
     "max_level": 5, "limited_level": 5, "multiplier": 2,
     "exclusive_set": [], "is_treasure": True, "supported_items": ["#minecraft:swords"]},
]

profile = {
    "name": "modded_sword",
    "description": "Modded sword enchantments (incremental, merge with vanilla)",
    "author": "benchmark",
    "version": "1.0.0",
    "schema_version": "2.1.0",
    # B-T13: incremental profile — declare the vanilla base so the effective
    # view (resolve_effective) merges builtin:vanilla + modded_sword, and so
    # `#minecraft:swords` supported_items resolve via the builtin tag universe.
    "dependencies": ["builtin:vanilla"],
    "enchantments": MOD_ENCHANTS,
    "equipments": [],
    "tags": [],
    "categories": []
}

out = os.path.join(os.path.dirname(__file__), "..", "data", "tests", "profiles", "modded_sword.json")
# 统一 LF 行尾（.gitattributes: * text=auto eol=lf）。
with open(out, "w", newline="\n") as f:
    json.dump(profile, f, indent=2, ensure_ascii=False)
print(f"Wrote: {out} ({len(MOD_ENCHANTS)} incremental enchants)")
