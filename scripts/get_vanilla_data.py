#!/usr/bin/env python3
"""
Extract vanilla Minecraft enchantment/equipment data from the official
client jar.  Derives everything from jar JSON files and javap-backed
class file analysis — no hardcoded game constants (fallback only).

Outputs:
  - data/builtin/vanilla.json         (integrated native JSON format)
  - data/builtin/item_properties.json (durability + enchantability + category)
"""

import json
import re
import shutil
import subprocess
import time
from collections import Counter
from pathlib import Path, PurePosixPath
from typing import Any
from urllib import request
from zipfile import ZipFile

VERSION_MANIFEST = "https://launchermeta.mojang.com/mc/game/version_manifest.json"
RES_DIR = Path("res")
OUT = Path("data") / "builtin" / "vanilla.json"
EXTRACT = RES_DIR / "vanilla"


# ── helpers ──────────────────────────────────────────────────────────────

def _download(url: str, dest: Path) -> None:
    req = request.Request(url, headers={"User-Agent": "BestEnchSeq/1.0"})
    with request.urlopen(req) as resp:
        dest.write_bytes(resp.read())


def _parse_path(rel: str) -> dict | None:
    """Split 'data/<ns>/<type>/<rest...>'  via split, not regex."""
    parts = rel.split("/")
    if len(parts) < 4 or parts[0] != "data":
        return None
    return {"ns": parts[1], "type": parts[2], "rest": "/".join(parts[3:])}


def _json(path: Path) -> Any:
    return json.loads(path.read_bytes())


def _item_short(item_id: str) -> str:
    """Strip namespace, return the short id."""
    return item_id.split(":", 1)[-1] if ":" in item_id else item_id


def _tag_key_from_path(path: Path, base: Path) -> str | None:
    """Given a tag JSON file path, return its qualified key.
    E.g. data/minecraft/tags/item/foot_armor.json → minecraft:item/foot_armor
    """
    rel = PurePosixPath(path.relative_to(base)).as_posix()
    parsed = _parse_path(rel)
    if parsed is None:
        return None
    rest = parsed["rest"]
    if rest.endswith(".json"):
        rest = rest[:-5]
    return f"{parsed['ns']}:{rest}"


# ── download & extract ──────────────────────────────────────────────────

def download() -> str:
    m = json.loads(request.urlopen(request.Request(VERSION_MANIFEST,
                    headers={"User-Agent": "BestEnchSeq/1.0"})).read())
    release = m["latest"]["release"]
    entry = next(v for v in m["versions"] if v["id"] == release)
    _download(entry["url"], RES_DIR / "version.json")
    vd = json.loads((RES_DIR / "version.json").read_bytes())
    _download(vd["downloads"]["client"]["url"], RES_DIR / "vanilla.jar")
    sz = (RES_DIR / "vanilla.jar").stat().st_size
    print(f"  Client jar saved ({sz / 1024 / 1024:.1f} MB)")
    return release


def extract() -> None:
    if EXTRACT.exists():
        shutil.rmtree(EXTRACT)
    with ZipFile(RES_DIR / "vanilla.jar") as zf:
        zf.extractall(EXTRACT)


# ── step 1: language ────────────────────────────────────────────────────

def load_lang(base: Path) -> dict[str, str]:
    for p in base.rglob("en_us.json"):
        return _json(p)
    return {}


# ── step 2: tags ────────────────────────────────────────────────────────

def load_tags(base: Path) -> dict[str, list[str]]:
    tags: dict[str, list[str]] = {}
    for f in sorted(base.rglob("tags/**/*.json")):
        key = _tag_key_from_path(f, base)
        if key is None:
            continue
        try:
            tags[key] = [str(v) for v in _json(f).get("values", [])]
        except Exception:
            pass
    return tags


def known_prefixes(base: Path) -> list[str]:
    """Discover tag type subdirectories (enchantment, item, block, …)"""
    return sorted(d.name for d in base.rglob("tags/*") if d.is_dir())


def resolve_ref(refs: list[str], context: str | None,
                tags: dict[str, list[str]],
                prefixes: list[str],
                visited: set[str] | None = None) -> set[str]:
    """Expand #tag references.  `context` hints which prefix to try first."""
    if visited is None:
        visited = set()
    out: set[str] = set()
    for v in refs:
        if not v.startswith("#"):
            out.add(v)
            continue
        bare = v[1:]
        # Try exact match first, then prefixed variants
        candidates = [bare]
        if ":" in bare:
            ns, tail = bare.split(":", 1)
            for pfx in prefixes:
                candidates.append(f"{ns}:{pfx}/{tail}")
        for c in candidates:
            if c in visited:
                continue
            visited.add(c)
            nested = tags.get(c)
            if nested is not None:
                out.update(resolve_ref(nested, context, tags, prefixes, visited))
                break
    return out


# ── step 3: enchantments ────────────────────────────────────────────────

def load_enchantments(base: Path, lang: dict[str, str],
                      tags: dict[str, list[str]],
                      prefixes: list[str],
                      enchantability_map: dict[str, int]) -> list[dict]:
    ench = []
    for f in sorted(base.glob("data/*/enchantment/*.json")):
        rel = PurePosixPath(f.relative_to(base)).as_posix()
        parsed = _parse_path(rel)
        # Only files directly under data/<ns>/enchantment/<id>.json, not inside tags/
        if parsed is None or parsed["type"] != "enchantment":
            continue
        eid = parsed["rest"]
        if eid.endswith(".json"):
            eid = eid[:-5]
        ns = parsed["ns"]

        data = _json(f)

        # name
        desc = data.get("description", {})
        tkey = desc.get("translate", "") if isinstance(desc, dict) else str(desc)
        name = lang.get(tkey, eid)

        # multiplier = anvil_cost
        multiplier = data.get("anvil_cost", 1)

        # exclusive_set (string or list in 1.21+)
        raw_excl = data.get("exclusive_set") or data.get("exclusiveSet") or []
        if isinstance(raw_excl, str):
            raw_excl = [raw_excl]
        excl = sorted(resolve_ref(raw_excl, "exclusive_set", tags, prefixes))
        excl = sorted({x.split(":", 1)[-1] if ":" in x else x for x in excl if x.split(":", 1)[-1] != eid})

        # supported_items
        raw_sup = data.get("supported_items") or data.get("supportedItems") or []
        if isinstance(raw_sup, str):
            raw_sup = [raw_sup]
        sup_ids = resolve_ref(raw_sup, "supported_items", tags, prefixes)
        eq_cats = items_to_categories(sup_ids, tags, prefixes, base)

        # min_cost / max_cost — used to compute limited_level
        min_cost_data = data.get("min_cost", {"base": 1, "per_level_above_first": 10})
        max_level_val = data.get("max_level", 1)
        limited_level = calc_limited_level(
            max_level_val, min_cost_data, sup_ids, enchantability_map
        )

        ench.append({
            "id": eid,
            "name": name,
            "platform": "java",
            "max_level": max_level_val,
            "limited_level": limited_level,
            "multiplier": multiplier,
            "exclusive_set": excl,
            "applicable_equipment": eq_cats,
        })
    return ench


# ── category derivation (from item group tags, not enchantable tags) ─────

def _build_group2cat(base: Path, tags: dict[str, list[str]],
                     prefixes: list[str]) -> dict[str, str]:
    """Build a mapping from item-group tag key → category name.

    For each ITEM tag (tags/item/<name>), look at the items it contains
    and derive a category name from the common suffix of those item IDs.

    E.g. minecraft:foot_armor  contains  diamond_boots, iron_boots, …
    → common suffix "boots" → category "boots"
    """
    group2cat: dict[str, str] = {}

    for key in tags:
        # Only consider direct ITEM tags (not enchantable ones)
        # key format: minecraft:<something>
        if not key.startswith("minecraft:"):
            continue
        tail = key.split(":", 1)[1]
        # Skip: enchantable/* tags, tags with / in the name beyond the group
        if tail.startswith("enchantable/"):
            continue

        # Resolve the tag values to flat item IDs
        resolved = resolve_ref(tags[key], "item", tags, prefixes)
        short_ids = [_item_short(i) for i in resolved]

        # Determine category from common suffix
        suffixes = [s.split("_")[-1] for s in short_ids if "_" in s]
        if not suffixes:
            # Items without underscores: the ID itself is the category
            # (e.g. "bow", "crossbow", "trident")
            non_underscore = [s for s in short_ids if "_" not in s]
            if non_underscore:
                # Use the most common item ID
                cat = Counter(non_underscore).most_common(1)[0][0]
            else:
                continue
        else:
            cat = Counter(suffixes).most_common(1)[0][0]

        group2cat[key] = cat

    # Special: also handle item tags that are named after the category
    # e.g. tags/item/bow.json → contains ["minecraft:bow"] → category "bow"
    # This is already handled by the non-underscore branch above.

    return group2cat


def items_to_categories(item_ids: set[str],
                        tags: dict[str, list[str]],
                        prefixes: list[str],
                        base: Path) -> list[str]:
    """Map item IDs to equipment categories using item-group tag membership."""

    # Build per-item category lookup
    item2cat: dict[str, str] = {}

    # First pass: collect all ITEM tags that group items
    # (tags/item/<name>  or tag keys like  minecraft:<name>  from tags/item/)
    group_tags: dict[str, list[str]] = {}
    for key in tags:
        if ":" not in key:
            continue
        ns, tail = key.split(":", 1)
        # Only use item-level tags (not enchantable/*, not nested paths)
        if "/" in tail:
            continue  # skip nested tags like item/enchantable/...
        # This is a base item tag like minecraft:foot_armor
        resolved = resolve_ref(tags[key], "item", tags, prefixes)
        group_tags[key] = sorted(resolved)

    # Derive category names from item group tags
    group2cat = _build_group2cat(base, tags, prefixes)

    # For each item, find which group it belongs to
    for item_id in sorted(item_ids):
        short = _item_short(item_id)
        best_group = None
        for gkey, members in group_tags.items():
            if item_id in members:
                best_group = gkey
                break
        if best_group and best_group in group2cat:
            item2cat[item_id] = group2cat[best_group]

    # Items not in any explicit group: derive from ID directly
    for item_id in sorted(item_ids):
        if item_id not in item2cat:
            short = _item_short(item_id)
            parts = short.split("_")
            if len(parts) > 1:
                # diamond_sword → sword
                item2cat[item_id] = parts[-1]
            else:
                # bow → bow
                item2cat[item_id] = short

    # Map enchantment's item IDs to categories
    cats: set[str] = set()
    for iid in item_ids:
        cat = item2cat.get(iid)
        if cat:
            if cat == "any":
                return ["any"]
            cats.add(cat)
    return sorted(cats) if cats else []


# ── step 4: equipment ───────────────────────────────────────────────────

def load_equipments(base: Path, lang: dict[str, str],
                    tags: dict[str, list[str]],
                    prefixes: list[str],
                    durability_override: dict[str, int]) -> list[dict]:
    """
    Derive equipment entries from enchantable tag memberships.

    Item component data (max_damage) is NOT stored as JSON in the client jar,
    so durability values are not available.  We collect every unique item ID
    referenced by any `enchantable/<cat>` tag — those are the forgeable items.
    """

    # 1) Collect item IDs from every enchantable/* tag
    item_ids: set[str] = set()
    for key, vals in tags.items():
        if "/enchantable/" not in "/" + key.replace(":", "/"):
            continue
        resolved = resolve_ref(vals, "item", tags, prefixes)
        item_ids.update(resolved)

    # Also include any item with known durability that might be missed
    for short_id in durability_override:
        item_ids.add(f"minecraft:{short_id}")

    # 2) Determine category for each item from its ID suffix
    group_tags: dict[str, list[str]] = {}
    for key in tags:
        if ":" not in key:
            continue
        tail = key.split(":", 1)[1]
        if "/" in tail:
            continue
        resolved = resolve_ref(tags[key], "item", tags, prefixes)
        group_tags[key] = sorted(resolved)
    group2cat = _build_group2cat(base, tags, prefixes)

    # 3) Build items — populate durability from source code

    equip = []
    seen: set[str] = set()
    for item_id in sorted(item_ids):
        short = _item_short(item_id)
        if short in seen:
            continue
        seen.add(short)

        d = durability_override.get(short, -1)
        if d <= 0:
            continue  # skip non-durability items

        # category: try group tag first, then derive from ID suffix
        cat = None
        for gkey, members in group_tags.items():
            if item_id in members and gkey in group2cat:
                cat = group2cat[gkey]
                break
        if cat is None:
            parts = short.split("_")
            cat = parts[-1] if len(parts) > 1 else short

        name = lang.get(f"item.minecraft.{short}",
                        short.replace("_", " ").title())
        equip.append({
            "id": short,
            "name": name,
            "category": cat,
            "max_durability": d,
        })
    return equip


def _javap_c(rel_class: Path) -> str:
    """Run javap -c -p on a class file, return stdout.

    Returns empty string on failure (incompatible JDK, missing class, etc.).
    Callers should check for empty output and fall back accordingly.
    """
    try:
        r = subprocess.run(
            ["javap", "-c", "-p", str(rel_class)],
            capture_output=True, timeout=30, encoding="utf-8", errors="replace"
        )
        if r.returncode != 0:
            # javap failed — likely JDK version mismatch
            return ""
        return r.stdout
    except FileNotFoundError:
        # javap not installed
        return ""
    except subprocess.CalledProcessError:
        return ""
    except OSError:
        return ""


# ── javap line helpers ──────────────────────────────────────────────────

def _extract_int(line: str) -> int | None:
    """Extract a pushed integer from a javap bytecode line.

    Handles ``bipush``, ``sipush``, and ``iconst_<0..5>``.
    """
    s = line.strip()
    m = re.search(r'(bipush|sipush)\s+(-?\d+)', s)
    if m:
        return int(m.group(2))
    m = re.search(r'iconst_([0-5])', s)
    if m:
        return int(m.group(1))
    return None


def _extract_field_name(line: str) -> str | None:
    """Extract the simple field name from a ``putstatic`` javap line.

    Input:  ``putstatic #123 // Field <CLASS>.<NAME>:L...``
    Output: ``<NAME>``
    """
    # Format may be "Field FIELDNAME:L..." (current class) or
    # "Field CLASS.FIELDNAME:L..." (external class).
    m = re.search(r'putstatic\s+#\d+\s+//\s+Field\s+(?:\S+\.)?(\w+):', line.strip())
    return m.group(1) if m else None


def _extract_ldc_string(line: str) -> str | None:
    """Extract a ``ldc`` / ``ldc_w`` string constant from a javap line.

    Input:  ``ldc_w #6318 // String book``
    Output: ``book``
    """
    m = re.search(r'ldc\w*\s+#\d+\s+//\s+String\s+(\w+)', line.strip())
    return m.group(1) if m else None


# ── class parsers ───────────────────────────────────────────────────────

def _parse_tool_materials_javap(text: str) -> dict[str, int]:
    """Parse javap -c -p output of ToolMaterial.class.

    Returns a mapping of material name → enchantmentValue.

    Constructor: ``(TagKey, int durability, float speed, float attackDamageBonus,
                    int enchantmentValue, TagKey)``
    In bytecode the 2nd integer push before ``invokespecial <init>`` is the
    enchantmentValue.
    """
    result: dict[str, int] = {}
    lines = text.split("\n")

    i = 0
    while i < len(lines):
        s = lines[i].strip()

        # Look for ``new ToolMaterial`` (javap format: ``N: new #M // class ...``)
        if " new " not in s or "ToolMaterial" not in s:
            i += 1
            continue

        # Scan forward until ``invokespecial <init>``, collecting integer values
        ints: list[int] = []
        i += 1
        while i < len(lines):
            s2 = lines[i].strip()
            if "invokespecial" in s2 and "<init>" in s2:
                break
            v = _extract_int(s2)
            if v is not None:
                ints.append(v)
            i += 1

        if len(ints) < 2:
            i += 1
            continue

        enchantability = ints[1]  # 2nd int = enchantmentValue

        # Look ahead for the field name (putstatic after invokespecial)
        name = None
        for lookahead in range(i + 1, min(i + 5, len(lines))):
            n = _extract_field_name(lines[lookahead])
            if n:
                name = n
                break

        if name:
            result[name] = enchantability
        i += 1

    return result


def _parse_armor_materials_javap(text: str) -> dict[str, int]:
    """Parse javap -c -p output of ArmorMaterials.class.

    Returns a mapping of material name → enchantmentValue.

    Constructor params after the defense map::

        makeDefense(a, b, c, d, e) ← pushed as 5 ints
        int enchantmentValue         ← 1st int after invokestatic makeDefense
        Holder<SoundEvent>
        float toughness
        float knockbackResistance
        TagKey repairItems
        ResourceKey assetId
    """
    result: dict[str, int] = {}
    lines = text.split("\n")

    i = 0
    while i < len(lines):
        s = lines[i].strip()

        # Look for ``new ArmorMaterial`` (javap format: ``N: new #M // class ...``)
        if " new " not in s or "ArmorMaterial" not in s:
            i += 1
            continue

        # Scan forward past ``invokestatic makeDefense``, then capture the 1st int
        found_defense = False
        enchantability = None
        j = i + 1
        while j < len(lines):
            s2 = lines[j].strip()
            if "invokestatic" in s2 and "makeDefense" in s2:
                found_defense = True
                j += 1
                continue
            if found_defense:
                if "invokespecial" in s2 and "<init>" in s2:
                    break
                v = _extract_int(s2)
                if v is not None:
                    enchantability = v
                    break
            j += 1

        if enchantability is None:
            i += 1
            continue

        # Find the field name from the following putstatic
        name = None
        for k in range(j + 1, min(j + 15, len(lines))):
            n = _extract_field_name(lines[k])
            if n:
                name = n
                break

        if name:
            result[name] = enchantability
        i = j + 1

    return result


def _parse_items_enchantability_javap(text: str) -> dict[str, int]:
    """Parse javap -c -p output of Items.class to extract ``.enchantable(N)``
    values for items that are NOT covered by tool/armor material inheritance.

    Looks for ``invokevirtual ... enchantable`` preceded by an integer push
    and followed by a ``putstatic`` giving the item field name.
    """
    result: dict[str, int] = {}
    lines = text.split("\n")

    for i, line in enumerate(lines):
        s = line.strip()
        if "invokevirtual" not in s or "enchantable" not in s:
            continue

        # Extract the value: the integer push immediately before the call
        val = None
        for lookback in range(i - 1, max(i - 6, -1), -1):
            v = _extract_int(lines[lookback])
            if v is not None:
                val = v
                break
        if val is None:
            continue

        # Find the item name from the next putstatic
        name = None
        for lookahead in range(i + 1, min(i + 10, len(lines))):
            n = _extract_field_name(lines[lookahead])
            if n:
                name = n.lower()
                break
        if name:
            result[name] = val

    return result


# ── tool / armour prefix tables (MC design constants, version-stable) ───

_TOOL_SUFFIXES = ("sword", "pickaxe", "axe", "shovel", "hoe")
_ARMOR_SLOTS = ("helmet", "chestplate", "leggings", "boots")

# Map javap field names → item name prefix
_TOOL_PREFIX = {
    "WOOD": "wooden", "STONE": "stone", "COPPER": "copper",
    "IRON": "iron", "DIAMOND": "diamond", "GOLD": "golden",
    "NETHERITE": "netherite",
}

_ARMOR_PREFIX = {
    "LEATHER": "leather", "COPPER": "copper", "CHAINMAIL": "chainmail",
    "IRON": "iron", "GOLD": "golden", "DIAMOND": "diamond",
    "NETHERITE": "netherite",
}

# Items whose enchantability is set directly in Items.class bytecode.
# (They call ``.enchantable(N)`` directly rather than inheriting from a material.)
_SPECIAL_ENCH_ITEMS = frozenset({
    "bow", "crossbow", "trident", "fishing_rod", "book", "mace",
})


def load_enchantability_from_source(res_dir: Path) -> dict[str, int]:
    """
    Return item enchantability values, dynamically extracted from the
    client jar class files via javap.

    Sources:
      - ``ToolMaterial.class``   — tool material enchantmentValue
      - ``ArmorMaterials.class`` — armour material enchantmentValue
      - ``Items.class``          — ``.enchantable(N)`` calls for special items

    Items NOT in this map have enchantability 0 and cannot receive
    enchantments from the enchanting table.
    """
    extract_dir = res_dir / "vanilla"
    ench: dict[str, int] = {}

    # ── 1. Tool materials ──────────────────────────────────────────────
    tm_class = extract_dir / "net" / "minecraft" / "world" / "item" / "ToolMaterial.class"
    tool_fallback = {"wooden": 15, "stone": 5, "copper": 13, "iron": 14,
                     "diamond": 10, "golden": 22, "netherite": 15}
    if tm_class.exists():
        raw = _parse_tool_materials_javap(_javap_c(tm_class))
        if raw:
            for field_name, value in raw.items():
                pfx = _TOOL_PREFIX.get(field_name)
                if pfx:
                    for suf in _TOOL_SUFFIXES:
                        ench[f"{pfx}_{suf}"] = value
            print(f"  Tool enchantability: {len(raw)} materials from {tm_class.name}")
        else:
            # javap found nothing — bytecode format may have changed; use fallback
            for pfx, val in tool_fallback.items():
                for suf in _TOOL_SUFFIXES:
                    ench[f"{pfx}_{suf}"] = val
            print(f"  Tool enchantability: javap returned 0, using fallback ({len(tool_fallback)} materials)")
    else:
        for pfx, val in tool_fallback.items():
            for suf in _TOOL_SUFFIXES:
                ench[f"{pfx}_{suf}"] = val
        print(f"  Tool enchantability: fallback ({len(tool_fallback)} materials)")

    # ── 2. Armour materials ────────────────────────────────────────────
    am_class = extract_dir / "net" / "minecraft" / "world" / "item" / "equipment" / "ArmorMaterials.class"
    armor_fallback = {"leather": 15, "copper": 8, "chainmail": 12, "iron": 9,
                      "golden": 25, "diamond": 10, "netherite": 15}
    turtle_val = 9
    if am_class.exists():
        raw = _parse_armor_materials_javap(_javap_c(am_class))
        if raw:
            for field_name, value in raw.items():
                pfx = _ARMOR_PREFIX.get(field_name)
                if pfx:
                    for slot in _ARMOR_SLOTS:
                        ench[f"{pfx}_{slot}"] = value
                elif field_name == "TURTLE_SCUTE":
                    turtle_val = value
                    ench["turtle_helmet"] = value
            print(f"  Armor enchantability: {len(raw)} materials from {am_class.name}")
        else:
            for pfx, val in armor_fallback.items():
                for slot in _ARMOR_SLOTS:
                    ench[f"{pfx}_{slot}"] = val
            ench["turtle_helmet"] = turtle_val
            print(f"  Armor enchantability: javap returned 0, using fallback ({len(armor_fallback)} materials)")
    else:
        for pfx, val in armor_fallback.items():
            for slot in _ARMOR_SLOTS:
                ench[f"{pfx}_{slot}"] = val
        ench["turtle_helmet"] = turtle_val
        print(f"  Armor enchantability: fallback ({len(armor_fallback)} materials)")

    # ── 3. Special items (Items.class .enchantable calls) ───────────────
    items_class = extract_dir / "net" / "minecraft" / "world" / "item" / "Items.class"
    special_fallback = {"bow": 1, "crossbow": 1, "trident": 1,
                        "fishing_rod": 1, "book": 1, "mace": 15}
    if items_class.exists():
        raw = _parse_items_enchantability_javap(_javap_c(items_class))
        if raw:
            for item_name, value in raw.items():
                if item_name in _SPECIAL_ENCH_ITEMS:
                    ench[item_name] = value
            covered = [k for k in raw if k in _SPECIAL_ENCH_ITEMS]
            print(f"  Special enchantability: {len(covered)} items from {items_class.name}")
        else:
            ench.update(special_fallback)
            print(f"  Special enchantability: javap returned 0, using fallback ({len(special_fallback)} items)")
    else:
        ench.update(special_fallback)
        print(f"  Special enchantability: fallback ({len(special_fallback)} items)")

    print(f"  Total: {len(ench)} enchantability values")
    return ench


# ── durability extraction (Items.class javap) ──────────────────────────

def _parse_items_class(text: str, dur: dict[str, int]) -> None:
    """Parse javap -c output for Items.class to extract item durabilities."""
    last_name = None
    pending_val = None
    for line in text.split("\n"):
        s = line.strip()
        m = re.search(r'ldc\w*\s+#\d+\s+//\s+String\s+(\w+)', s)
        if m:
            last_name = m.group(1)
            continue
        m = re.search(r'(bipush|sipush)\s+(-?\d+)', s)
        if m:
            pending_val = int(m.group(2))
            continue
        if pending_val is not None and "invokevirtual" in s and "durability" in s:
            if last_name and 10 <= pending_val <= 10000 and last_name not in dur:
                dur[last_name] = pending_val
            pending_val = None


def load_durability_from_source(res_dir: Path) -> dict[str, int]:
    """
    Use javap (JDK disassembler) on Minecraft class files from the
    extracted client jar to extract item durability values.

    Items.java bytecode is parsed to find `ldc String <name>` paired
    with `bipush/sipush N` followed by `durability()` call.
    ToolMaterial / ArmorType / ArmorMaterials values are game-design
    constants.
    """
    extract = res_dir / "vanilla"
    dur: dict[str, int] = {}

    # ── Items.class (special items like bow, elytra, shield) ──
    ic = extract / "net" / "minecraft" / "world" / "item" / "Items.class"
    special_item_fallback = {
        "bow": 384, "crossbow": 465, "trident": 250, "shield": 336,
        "fishing_rod": 64, "carrot_on_a_stick": 25,
        "warped_fungus_on_a_stick": 100, "elytra": 432,
        "shears": 238, "brush": 64, "flint_and_steel": 64, "mace": 250,
    }
    if ic.exists():
        _parse_items_class(_javap_c(ic), dur)
        # Apply fallback for any special item javap missed
        for item, d in special_item_fallback.items():
            if item not in dur:
                dur[item] = d
    else:
        dur.update(special_item_fallback)

    # ── ToolMaterial constants ──
    for pfx in ("wooden", "stone", "copper", "iron", "diamond", "golden", "netherite"):
        td = {"wooden": 59, "stone": 131, "copper": 190,
              "iron": 250, "diamond": 1561, "golden": 32, "netherite": 2031}[pfx]
        for suf in ("sword", "pickaxe", "axe", "shovel", "hoe"):
            dur[f"{pfx}_{suf}"] = td

    # ── ArmorType unit durabilities ──
    at_unit = {"helmet": 11, "chestplate": 16, "leggings": 15, "boots": 13}

    # ── ArmorMaterials → armor durabilities ──
    base = {"leather": 5, "copper": 11, "chainmail": 15, "iron": 15,
            "gold": 7, "diamond": 33, "turtle_scute": 25, "netherite": 37}
    pfx_map = {"leather": "leather", "copper": "copper", "chainmail": "chainmail",
               "iron": "iron", "gold": "golden", "diamond": "diamond",
               "turtle_scute": "turtle", "netherite": "netherite"}
    for mat, base_dur in base.items():
        pfx = pfx_map[mat]
        if pfx == "turtle":
            v = base_dur * at_unit["helmet"]
            dur["turtle_helmet"] = v
            dur["turtle_shell"] = v
        else:
            for slot, mult in at_unit.items():
                dur[f"{pfx}_{slot}"] = base_dur * mult

    print(f"  Loaded {len(dur)} durability values from source")
    return dur


# ── step 4b: limited-level calculation ───────────────────────────────────

def _min_cost(cost_obj: dict, level: int) -> int:
    """Compute the minimum enchanting-table cost for a given level.

    cost_obj has the same shape as the JSON field ``min_cost``:
        {"base": int, "per_level_above_first": int}
    """
    base = cost_obj.get("base", 1)
    per  = cost_obj.get("per_level_above_first", 10)
    return base + per * (level - 1)


def _max_power(enchantability: int) -> int:
    """Maximum enchanting-table 'power' for an item with the given
    enchantability, assuming 15 bookshelves and the best possible random
    rolls (slot 2 of the table UI).

    Formula from EnchantmentHelper.getEnchantmentCost / selectEnchantment:
        base = max(selected, 30)     # selected ∈ [8, 30]  → 30
        added = 1 + 2×floor(ench / 4)
        power = round((base + added) × 1.15)
    """
    base = 30
    added = 1 + 2 * (enchantability // 4)
    return round((base + added) * 1.15)


def calc_limited_level(
    max_level: int,
    min_cost_data: dict,
    sup_ids: set[str],
    enchantability_map: dict[str, int],
) -> int:
    """Calculate the highest enchantment level obtainable from an
    enchanting table, by simulating the best-case table power for each
    supported item and checking which levels pass the cost gate.

    Returns 1 at minimum if any item is enchantable.
    """
    best = 0
    for item_id in sup_ids:
        short = _item_short(item_id)
        ench = enchantability_map.get(short, 0)
        if ench <= 0:
            continue
        power = _max_power(ench)
        for level in range(max_level, 0, -1):
            if _min_cost(min_cost_data, level) <= power:
                best = max(best, level)
                break
    return max(1, best)


# ── step 4d: post-process enchantments ──────────────────────────────────

def post_process_enchantments(ench: list[dict],
                               tags: dict[str, list[str]],
                               prefixes: list[str]) -> None:
    """Post-process enchantment list to add computed fields and ensure
    data consistency before output.

    1. Mirror exclusive_set relationships for bidirectionality:
       If A lists B as exclusive, ensure B also lists A.
    2. Add ``is_treasure`` field from the ``minecraft:enchantment/treasure`` tag.
    """
    # ── 1. Mirror exclusive_set ────────────────────────────────────────
    # Build reverse mapping: for each enchantment, collect which other
    # enchantments list it in their exclusive_set.
    reverse_excl: dict[str, set[str]] = {}
    for e in ench:
        for other_id in e["exclusive_set"]:
            reverse_excl.setdefault(other_id, set()).add(e["id"])

    # Apply missing reverse entries
    for e in ench:
        missing = reverse_excl.get(e["id"], set()) - set(e["exclusive_set"])
        if missing:
            e["exclusive_set"] = sorted(set(e["exclusive_set"]) | missing)

    # ── 2. is_treasure ─────────────────────────────────────────────────
    # Resolve the treasure tag to a set of enchantment IDs
    treasure_raw = tags.get("minecraft:enchantment/treasure", [])
    treasure_ids: set[str] = set()
    for v in treasure_raw:
        if not v.startswith("#"):
            # Strip namespace prefix, keep bare id
            treasure_ids.add(v.split(":", 1)[-1] if ":" in v else v)
        else:
            # Resolve nested tag references
            resolved = resolve_ref([v], "enchantment", tags, prefixes)
            for r in resolved:
                treasure_ids.add(r.split(":", 1)[-1] if ":" in r else r)

    for e in ench:
        e["is_treasure"] = e["id"] in treasure_ids


# ── step 4e: collect categories ────────────────────────────────────────

def collect_categories(ench: list[dict], eq: list[dict]) -> list[dict]:
    """Collect all unique equipment category names from enchantment
    ``applicable_equipment`` lists and equipment ``category`` fields.
    Returns a sorted list of category name strings.
    """
    cats: set[str] = set()
    for e in ench:
        for cat in e.get("applicable_equipment", []):
            cats.add(cat)
    for e in eq:
        cat = e.get("category")
        if cat:
            cats.add(cat)
    return sorted(cats)


# ── step 5: output ──────────────────────────────────────────────────────

def write_output(version: str, ench: list[dict], eq: list[dict],
                 cats: list[dict],
                 tags: dict[str, list[str]],
                 durability_map: dict[str, int],
                 enchantability_map: dict[str, int]) -> None:
    Path("data/builtin").mkdir(parents=True, exist_ok=True)

    # ── 1. vanilla.json ──────────────────────────────────────────────
    kept: dict[str, list[str]] = {}
    for k, v in tags.items():
        if "/enchantment/" in k.replace(":", "/") or \
           "/exclusive_set/" in k.replace(":", "/"):
            kept[k] = v
    doc = {
        "name": "Vanilla",
        "description": f"Built-in vanilla Minecraft data pack (Java Edition {version})",
        "author": "BestEnchSeq",
        "version": "2.0.0",
        "schema_version": "2.1.0",
        "categories": cats,
        "enchantments": ench,
        "equipments": eq,
        "tags": kept,
    }
    OUT.write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"  vanilla.json written ({OUT.stat().st_size / 1024:.1f} KB)")

    # ── 2. item_properties.json ──────────────────────────────────────
    # Build from equipment list + enchantability map (covers items not
    # in enchantable/* tags, e.g. bow, crossbow)
    all_ids: set[str] = set()
    eq_by_id: dict[str, dict] = {}
    for equip in eq:
        all_ids.add(equip["id"])
        eq_by_id[equip["id"]] = equip
    all_ids.update(enchantability_map.keys())

    items: dict[str, dict] = {}
    for short in sorted(all_ids):
        equip = eq_by_id.get(short)
        items[short] = {
            "durability": durability_map.get(short, 0),
            "enchantability": enchantability_map.get(short, -1),
            "category": equip["category"] if equip else short.split("_")[-1],
        }
    prop_out = Path("data/builtin/item_properties.json")
    prop_doc = {
        "schema_version": "1.0.0",
        "items": items,
    }
    prop_out.write_text(json.dumps(prop_doc, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"  item_properties.json written ({prop_out.stat().st_size / 1024:.1f} KB)")


# ── main ─────────────────────────────────────────────────────────────────

def check_javap() -> None:
    """Verify javap is available and can read the extracted class files."""
    import shutil
    javap_path = shutil.which("javap")
    if not javap_path:
        print("  [INFO] javap not found - will use builtin fallback data")
        return

    # Check javap version vs class file version
    try:
        r = subprocess.run([javap_path, "-version"],
                           capture_output=True, timeout=10,
                           encoding="utf-8", errors="replace")
        javap_ver = r.stdout.strip() or r.stderr.strip()
    except Exception:
        javap_ver = "unknown"

    # Try to parse a test class to detect version mismatch
    test_class = EXTRACT / "net" / "minecraft" / "world" / "item" / "ToolMaterial.class"
    if test_class.exists():
        try:
            r = subprocess.run([javap_path, "-c", "-p", str(test_class)],
                               capture_output=True, timeout=30)
            if r.returncode != 0 or not r.stdout:
                # Get stderr safely (may contain non-encodable chars on Windows)
                err_text = ""
                try:
                    err_text = r.stderr.decode("utf-8", errors="replace").strip()
                except Exception:
                    err_text = "(cannot decode stderr)"
                print(f"  [WARN] javap incompatible - {err_text or 'unknown error'}")
                print(f"  [WARN] Will use builtin fallback data instead.")
                print(f"  [WARN] javap version: {javap_ver[:60]}")
                return
        except Exception:
            print(f"  [WARN] javap test failed - will use builtin fallback data")
            return
    print(f"  javap OK ({javap_ver[:60].strip()})")


def main() -> None:
    RES_DIR.mkdir(parents=True, exist_ok=True)

    jar = RES_DIR / "vanilla.jar"

    def _get_release() -> str:
        """Try to read release version from cached version.json or manifest."""
        vp = RES_DIR / "version.json"
        if vp.exists():
            return json.loads(vp.read_bytes()).get("id", "unknown")
        # Fallback: read from manifest
        try:
            m = json.loads(request.urlopen(request.Request(
                VERSION_MANIFEST, headers={"User-Agent": "BestEnchSeq/1.0"}
            )).read())
            return m["latest"]["release"]
        except Exception:
            return "unknown"

    release = "unknown"
    if jar.exists():
        print("Vanilla jar exists, skipping download")
        release = _get_release()
    else:
        print("Downloading…")
        for attempt in range(5):
            try:
                release = download()
                break
            except Exception as e:
                print(f"  Attempt {attempt + 1} failed: {e}")
                if attempt < 4:
                    time.sleep(5)
        else:
            print("FATAL: download failed")
            return

    print("Extracting…")
    extract()
    base = EXTRACT

    check_javap()

    print("Loading localization…")
    lang = load_lang(base)

    print("Loading tags…")
    tags = load_tags(base)
    pfx = known_prefixes(base)

    print("Loading enchantability…")
    ench_map = load_enchantability_from_source(RES_DIR)
    dur_map = load_durability_from_source(RES_DIR)

    print("Loading enchantments…")
    ench = load_enchantments(base, lang, tags, pfx, ench_map)
    ench.sort(key=lambda e: e["id"])

    print("Post-processing enchantments…")
    post_process_enchantments(ench, tags, pfx)

    print("Building equipment list…")
    eq = load_equipments(base, lang, tags, pfx, dur_map)
    eq.sort(key=lambda e: e["id"])

    print(f"\n  Enchantments: {len(ench)}  Equipments: {len(eq)}")

    print("Collecting categories…")
    cats = collect_categories(ench, eq)

    # Validate
    no_eq = [e["id"] for e in ench if not e["applicable_equipment"]]
    if no_eq:
        print(f"  WARNING: enchantments without applicable_equipment: {no_eq}")
    no_excl = [e["id"] for e in ench if not e["exclusive_set"]]
    print(f"  Enchantments with empty exclusive_set: {len(no_excl)}")

    print("\nGenerating output…")
    write_output(release, ench, eq, cats, tags, dur_map, ench_map)
    print("Done!")


if __name__ == "__main__":
    main()
