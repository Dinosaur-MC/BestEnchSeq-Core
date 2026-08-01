#!/usr/bin/env python3
"""
Generate benchmark data file with vanilla + modded enchantments for large test cases.

Reads data/builtin/vanilla.json and adds synthetic mod-style enchantments
that are compatible with specific equipment categories, enabling 12-16 ench
test cases in forge_benchmark.
"""

import json, copy, sys, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
DATA_FILE = os.path.join(PROJECT_DIR, "data", "builtin", "vanilla.json")
OUTPUT_FILE = os.path.join(PROJECT_DIR, "data", "tests", "benchmark_plus.json")

# ── Additional mod-style enchantments ──────────────────────────────────────
# These are inspired by Enchantments Encore & Enchantology, formatted for the
# builtin JSON schema (v2.1.0).  All are sword-compatible with no exclusive_set
# so they can coexist with vanilla sword enchants for large test cases.
MOD_ENCHANTS = [
    {
        "id": "leeching",
        "name": "Leeching",
        "platform": "java",
        "max_level": 2,
        "limited_level": 2,
        "multiplier": 4,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "brutality",
        "name": "Brutality",
        "platform": "java",
        "max_level": 3,
        "limited_level": 3,
        "multiplier": 3,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "dexterity",
        "name": "Dexterity",
        "platform": "java",
        "max_level": 3,
        "limited_level": 3,
        "multiplier": 2,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "gnashing",
        "name": "Gnashing",
        "platform": "java",
        "max_level": 3,
        "limited_level": 3,
        "multiplier": 4,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "shattering",
        "name": "Shattering",
        "platform": "java",
        "max_level": 2,
        "limited_level": 2,
        "multiplier": 3,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "subjugation",
        "name": "Subjugation",
        "platform": "java",
        "max_level": 3,
        "limited_level": 3,
        "multiplier": 4,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "thunderbolting",
        "name": "Thunderbolting",
        "platform": "java",
        "max_level": 1,
        "limited_level": 1,
        "multiplier": 6,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "poison_aspect",
        "name": "Poison Aspect",
        "platform": "java",
        "max_level": 2,
        "limited_level": 2,
        "multiplier": 3,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "wither_aspect",
        "name": "Wither Aspect",
        "platform": "java",
        "max_level": 2,
        "limited_level": 2,
        "multiplier": 4,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "swift_slash",
        "name": "Swift Slash",
        "platform": "java",
        "max_level": 3,
        "limited_level": 3,
        "multiplier": 2,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "velocity",
        "name": "Velocity",
        "platform": "java",
        "max_level": 3,
        "limited_level": 3,
        "multiplier": 2,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "explosive",
        "name": "Explosive",
        "platform": "java",
        "max_level": 2,
        "limited_level": 2,
        "multiplier": 5,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "windshear",
        "name": "Windshear",
        "platform": "java",
        "max_level": 2,
        "limited_level": 2,
        "multiplier": 3,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
    {
        "id": "immolation",
        "name": "Immolation",
        "platform": "java",
        "max_level": 2,
        "limited_level": 2,
        "multiplier": 3,
        "exclusive_set": [],
        "supported_items": ["#minecraft:swords"],
        "is_treasure": True
    },
]


def main():
    with open(DATA_FILE, "r", encoding="utf-8") as f:
        data = json.load(f)

    existing_ids = {e["id"] for e in data["enchantments"]}
    added = 0
    for me in MOD_ENCHANTS:
        if me["id"] in existing_ids:
            print(f"  SKIP (already exists): {me['id']}")
            continue
        data["enchantments"].append(me)
        added += 1

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)

    print(f"Generated: {OUTPUT_FILE}")
    print(f"  Vanilla enchantments: {len(data['enchantments']) - added}")
    print(f"  Added mod enchants:   {added}")
    print(f"  Total enchantments:   {len(data['enchantments'])}")


if __name__ == "__main__":
    main()
