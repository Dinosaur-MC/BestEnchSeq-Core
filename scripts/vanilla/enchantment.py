"""Enchantment and equipment data extraction from the extracted client jar.

Parses the extracted jar's JSON data files (tags, enchantment definitions)
and optionally uses javap to extract enchantability / durability values
from compiled class files.
"""

from __future__ import annotations

import json
import re
import subprocess
from collections import Counter
from pathlib import Path, PurePosixPath
from typing import Any

from . import meta


# ── output paths ──────────────────────────────────────────────────────────

OUT_VANILLA = Path("data") / "builtin" / "vanilla.json"
OUT_ITEM_PROPS = Path("data") / "builtin" / "item_properties.json"


# ── small helpers ─────────────────────────────────────────────────────────

def _parse_path(rel: str) -> dict | None:
    """Split ``data/<ns>/<type>/<rest...>`` via split, not regex."""
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

    E.g.  ``data/minecraft/tags/item/foot_armor.json``
          →  ``minecraft:item/foot_armor``
    """
    rel = PurePosixPath(path.relative_to(base)).as_posix()
    parsed = _parse_path(rel)
    if parsed is None:
        return None
    rest = parsed["rest"]
    if rest.endswith(".json"):
        rest = rest[:-5]
    return f"{parsed['ns']}:{rest}"


# ── step 1: load base language file from jar ──────────────────────────────

def load_lang(base: Path) -> dict[str, str]:
    """Load ``en_us.json`` from the extracted jar."""
    for p in base.rglob("en_us.json"):
        return _json(p)
    return {}


# ── step 2: tags ──────────────────────────────────────────────────────────

def load_tags(base: Path) -> dict[str, list[str]]:
    """Load all tag JSON files from the extracted jar."""
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
    """Discover tag type subdirectories (enchantment, item, block, …)."""
    return sorted(d.name for d in base.rglob("tags/*") if d.is_dir())


def resolve_ref(refs: list[str], context: str | None,
                tags: dict[str, list[str]],
                prefixes: list[str],
                visited: set[str] | None = None) -> set[str]:
    """Expand ``#tag`` references.  ``context`` hints which prefix to try first."""
    if visited is None:
        visited = set()
    out: set[str] = set()
    for v in refs:
        if not v.startswith("#"):
            out.add(v)
            continue
        bare = v[1:]
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


# ── step 3: enchantments ─────────────────────────────────────────────────

def load_enchantments(base: Path, lang: dict[str, str],
                      tags: dict[str, list[str]],
                      prefixes: list[str],
                      enchantability_map: dict[str, int]) -> list[dict]:
    """Parse enchantment definition JSONs from the extracted jar."""
    ench = []
    for f in sorted(base.glob("data/*/enchantment/*.json")):
        rel = PurePosixPath(f.relative_to(base)).as_posix()
        parsed = _parse_path(rel)
        if parsed is None or parsed["type"] != "enchantment":
            continue
        eid = parsed["rest"]
        if eid.endswith(".json"):
            eid = eid[:-5]

        data = _json(f)

        # name via translation key
        desc = data.get("description", {})
        tkey = desc.get("translate", "") if isinstance(desc, dict) else str(desc)
        name = lang.get(tkey, eid)

        multiplier = data.get("anvil_cost", 1)

        # exclusive_set
        raw_excl = data.get("exclusive_set") or data.get("exclusiveSet") or []
        if isinstance(raw_excl, str):
            raw_excl = [raw_excl]
        excl = sorted(resolve_ref(raw_excl, "exclusive_set", tags, prefixes))
        excl = sorted({
            x.split(":", 1)[-1] if ":" in x else x
            for x in excl if x.split(":", 1)[-1] != eid
        })

        # supported_items → categories
        raw_sup = data.get("supported_items") or data.get("supportedItems") or []
        if isinstance(raw_sup, str):
            raw_sup = [raw_sup]
        sup_ids = resolve_ref(raw_sup, "supported_items", tags, prefixes)
        eq_cats = items_to_categories(sup_ids, tags, prefixes, base)

        # limited level
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


# ── category derivation (from item-group tags, not enchantable tags) ──────

def _build_group2cat(base: Path, tags: dict[str, list[str]],
                     prefixes: list[str]) -> dict[str, str]:
    """Build a mapping from item-group tag key → category name."""
    group2cat: dict[str, str] = {}

    for key in tags:
        if not key.startswith("minecraft:"):
            continue
        tail = key.split(":", 1)[1]
        if tail.startswith("enchantable/"):
            continue

        resolved = resolve_ref(tags[key], "item", tags, prefixes)
        short_ids = [_item_short(i) for i in resolved]

        suffixes = [s.split("_")[-1] for s in short_ids if "_" in s]
        if not suffixes:
            non_underscore = [s for s in short_ids if "_" not in s]
            if non_underscore:
                cat = Counter(non_underscore).most_common(1)[0][0]
            else:
                continue
        else:
            cat = Counter(suffixes).most_common(1)[0][0]

        group2cat[key] = cat

    return group2cat


def items_to_categories(item_ids: set[str],
                        tags: dict[str, list[str]],
                        prefixes: list[str],
                        base: Path) -> list[str]:
    """Map item IDs to equipment categories using item-group tag membership."""
    item2cat: dict[str, str] = {}

    group_tags: dict[str, list[str]] = {}
    for key in tags:
        if ":" not in key:
            continue
        ns, tail = key.split(":", 1)
        if "/" in tail:
            continue
        resolved = resolve_ref(tags[key], "item", tags, prefixes)
        group_tags[key] = sorted(resolved)

    group2cat = _build_group2cat(base, tags, prefixes)

    for item_id in sorted(item_ids):
        short = _item_short(item_id)
        best_group = None
        for gkey, members in group_tags.items():
            if item_id in members:
                best_group = gkey
                break
        if best_group and best_group in group2cat:
            item2cat[item_id] = group2cat[best_group]

    for item_id in sorted(item_ids):
        if item_id not in item2cat:
            short = _item_short(item_id)
            parts = short.split("_")
            if len(parts) > 1:
                item2cat[item_id] = parts[-1]
            else:
                item2cat[item_id] = short

    cats: set[str] = set()
    for iid in item_ids:
        cat = item2cat.get(iid)
        if cat:
            if cat == "any":
                return ["any"]
            cats.add(cat)
    return sorted(cats) if cats else []


# ── step 4: equipment ─────────────────────────────────────────────────────

def load_equipments(base: Path, lang: dict[str, str],
                    tags: dict[str, list[str]],
                    prefixes: list[str],
                    durability_override: dict[str, int]) -> list[dict]:
    """Derive equipment entries from enchantable tag memberships."""
    # 1) Collect item IDs from every enchantable/* tag
    item_ids: set[str] = set()
    for key, vals in tags.items():
        if "/enchantable/" not in "/" + key.replace(":", "/"):
            continue
        resolved = resolve_ref(vals, "item", tags, prefixes)
        item_ids.update(resolved)
    for short_id in durability_override:
        item_ids.add(f"minecraft:{short_id}")

    # 2) Category lookup
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

    # 3) Build equipment list
    equip = []
    seen: set[str] = set()
    for item_id in sorted(item_ids):
        short = _item_short(item_id)
        if short in seen:
            continue
        seen.add(short)

        d = durability_override.get(short, -1)
        if d <= 0:
            continue

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


# ── javap helpers ─────────────────────────────────────────────────────────

def _javap_c(rel_class: Path) -> str:
    """Run ``javap -c -p`` on a class file, return stdout (empty on failure)."""
    try:
        r = subprocess.run(
            ["javap", "-c", "-p", str(rel_class)],
            capture_output=True, timeout=30, encoding="utf-8", errors="replace"
        )
        if r.returncode != 0:
            return ""
        return r.stdout
    except (FileNotFoundError, subprocess.CalledProcessError, OSError):
        return ""


def _extract_int(line: str) -> int | None:
    """Extract a pushed integer from a javap bytecode line."""
    s = line.strip()
    m = re.search(r'(bipush|sipush)\s+(-?\d+)', s)
    if m:
        return int(m.group(2))
    m = re.search(r'iconst_([0-5])', s)
    if m:
        return int(m.group(1))
    return None


def _extract_field_name(line: str) -> str | None:
    """Extract a field name from a ``putstatic`` javap line."""
    m = re.search(r'putstatic\s+#\d+\s+//\s+Field\s+(?:\S+\.)?(\w+):', line.strip())
    return m.group(1) if m else None


def _extract_ldc_string(line: str) -> str | None:
    """Extract an ``ldc`` / ``ldc_w`` string constant from a javap line."""
    m = re.search(r'ldc\w*\s+#\d+\s+//\s+String\s+(\w+)', line.strip())
    return m.group(1) if m else None


# ── javap class parsers ───────────────────────────────────────────────────

_TOOL_SUFFIXES = ("sword", "pickaxe", "axe", "shovel", "hoe")
_ARMOR_SLOTS = ("helmet", "chestplate", "leggings", "boots")

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
_SPECIAL_ENCH_ITEMS = frozenset({
    "bow", "crossbow", "trident", "fishing_rod", "book", "mace",
})


def _parse_tool_materials_javap(text: str) -> dict[str, int]:
    """Parse javap -c -p output of ToolMaterial.class.

    Returns a mapping of material name → enchantmentValue.
    """
    result: dict[str, int] = {}
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        if " new " not in s or "ToolMaterial" not in s:
            i += 1
            continue
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
        if len(ints) >= 2:
            enchantability = ints[1]
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
    """Parse javap -c -p output of ArmorMaterials.class."""
    result: dict[str, int] = {}
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        if " new " not in s or "ArmorMaterial" not in s:
            i += 1
            continue
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
        if enchantability is not None:
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
    """Parse javap -c -p output of Items.class for ``.enchantable(N)``."""
    result: dict[str, int] = {}
    lines = text.split("\n")
    for i, line in enumerate(lines):
        s = line.strip()
        if "invokevirtual" not in s or "enchantable" not in s:
            continue
        val = None
        for lookback in range(i - 1, max(i - 6, -1), -1):
            v = _extract_int(lines[lookback])
            if v is not None:
                val = v
                break
        if val is None:
            continue
        name = None
        for lookahead in range(i + 1, min(i + 10, len(lines))):
            n = _extract_field_name(lines[lookahead])
            if n:
                name = n.lower()
                break
        if name:
            result[name] = val
    return result


# ── enchantability ────────────────────────────────────────────────────────

def load_enchantability_from_source(res_dir: Path) -> dict[str, int]:
    """Return item enchantability values via javap class analysis.

    Falls back to hardcoded constants when javap is unavailable or the
    bytecode format has changed.
    """
    extract_dir = res_dir / "vanilla"
    ench: dict[str, int] = {}

    # 1. Tool materials
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
            for pfx, val in tool_fallback.items():
                for suf in _TOOL_SUFFIXES:
                    ench[f"{pfx}_{suf}"] = val
            print(f"  Tool enchantability: javap returned 0, using fallback ({len(tool_fallback)} materials)")
    else:
        for pfx, val in tool_fallback.items():
            for suf in _TOOL_SUFFIXES:
                ench[f"{pfx}_{suf}"] = val
        print(f"  Tool enchantability: fallback ({len(tool_fallback)} materials)")

    # 2. Armour materials
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

    # 3. Special items (Items.class .enchantable calls)
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


# ── durability ────────────────────────────────────────────────────────────

def _parse_items_class(text: str, dur: dict[str, int]) -> None:
    """Parse javap -c output for Items.class to extract durabilities."""
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
    """Return item durability values via javap + design constants."""
    extract = res_dir / "vanilla"
    dur: dict[str, int] = {}

    # Items.class (special items)
    ic = extract / "net" / "minecraft" / "world" / "item" / "Items.class"
    special_item_fallback = {
        "bow": 384, "crossbow": 465, "trident": 250, "shield": 336,
        "fishing_rod": 64, "carrot_on_a_stick": 25,
        "warped_fungus_on_a_stick": 100, "elytra": 432,
        "shears": 238, "brush": 64, "flint_and_steel": 64, "mace": 250,
    }
    if ic.exists():
        _parse_items_class(_javap_c(ic), dur)
        for item, d in special_item_fallback.items():
            if item not in dur:
                dur[item] = d
    else:
        dur.update(special_item_fallback)

    # Tool material durabilities
    for pfx in ("wooden", "stone", "copper", "iron", "diamond", "golden", "netherite"):
        td = {"wooden": 59, "stone": 131, "copper": 190,
              "iron": 250, "diamond": 1561, "golden": 32, "netherite": 2031}[pfx]
        for suf in ("sword", "pickaxe", "axe", "shovel", "hoe"):
            dur[f"{pfx}_{suf}"] = td

    # Armor durabilities
    at_unit = {"helmet": 11, "chestplate": 16, "leggings": 15, "boots": 13}
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


# ── limited-level calculation ─────────────────────────────────────────────

def _min_cost(cost_obj: dict, level: int) -> int:
    """Compute minimum enchanting-table cost for a given level."""
    base = cost_obj.get("base", 1)
    per = cost_obj.get("per_level_above_first", 10)
    return base + per * (level - 1)


def _max_power(enchantability: int) -> int:
    """Maximum enchanting-table power (15 bookshelves, best slot)."""
    base = 30
    added = 1 + 2 * (enchantability // 4)
    return round((base + added) * 1.15)


def calc_limited_level(max_level: int, min_cost_data: dict,
                       sup_ids: set[str],
                       enchantability_map: dict[str, int]) -> int:
    """Highest enchantment level obtainable from an enchanting table."""
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


# ── post-processing ───────────────────────────────────────────────────────

def post_process_enchantments(ench: list[dict],
                              tags: dict[str, list[str]],
                              prefixes: list[str]) -> None:
    """Mirror exclusive_set relationships and add ``is_treasure``."""
    # Mirror exclusive_set
    reverse_excl: dict[str, set[str]] = {}
    for e in ench:
        for other_id in e["exclusive_set"]:
            reverse_excl.setdefault(other_id, set()).add(e["id"])
    for e in ench:
        missing = reverse_excl.get(e["id"], set()) - set(e["exclusive_set"])
        if missing:
            e["exclusive_set"] = sorted(set(e["exclusive_set"]) | missing)

    # is_treasure
    treasure_raw = tags.get("minecraft:enchantment/treasure", [])
    treasure_ids: set[str] = set()
    for v in treasure_raw:
        if not v.startswith("#"):
            treasure_ids.add(v.split(":", 1)[-1] if ":" in v else v)
        else:
            resolved = resolve_ref([v], "enchantment", tags, prefixes)
            for r in resolved:
                treasure_ids.add(r.split(":", 1)[-1] if ":" in r else r)
    for e in ench:
        e["is_treasure"] = e["id"] in treasure_ids


# ── collect categories ────────────────────────────────────────────────────

def collect_categories(ench: list[dict], eq: list[dict]) -> list[str]:
    """Collect all unique equipment category names."""
    cats: set[str] = set()
    for e in ench:
        for cat in e.get("applicable_equipment", []):
            cats.add(cat)
    for e in eq:
        cat = e.get("category")
        if cat:
            cats.add(cat)
    return sorted(cats)


# ── output ────────────────────────────────────────────────────────────────

def write_output(version: str, ench: list[dict], eq: list[dict],
                 cats: list[str],
                 tags: dict[str, list[str]],
                 durability_map: dict[str, int],
                 enchantability_map: dict[str, int]) -> None:
    """Write vanilla.json and item_properties.json."""
    Path("data/builtin").mkdir(parents=True, exist_ok=True)

    # vanilla.json
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
    OUT_VANILLA.write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"  vanilla.json written ({OUT_VANILLA.stat().st_size / 1024:.1f} KB)")

    # item_properties.json
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
    prop_doc = {"schema_version": "1.0.0", "items": items}
    OUT_ITEM_PROPS.write_text(json.dumps(prop_doc, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"  item_properties.json written ({OUT_ITEM_PROPS.stat().st_size / 1024:.1f} KB)")
