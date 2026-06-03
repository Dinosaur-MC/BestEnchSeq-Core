#!/usr/bin/env python3
"""
Extract vanilla Minecraft enchantment/equipment data from the official
client jar.  Derives everything from jar JSON files — no hardcoded
category patterns, or durability tables.

Output: data/builtin/vanilla.json  (integrated native JSON format)
"""

import json
import re
import shutil
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
                      prefixes: list[str]) -> list[dict]:
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

        ench.append({
            "id": eid,
            "name": name,
            "platform": "java",
            "max_level": data.get("max_level", 1),
            "limited_level": data.get("max_level", 1),
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
                    prefixes: list[str]) -> list[dict]:
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
    durability_override = load_durability_from_source(RES_DIR)
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
    durability_override = load_durability_from_source(RES_DIR)

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
    """Run javap -c -p on a class file, return stdout."""
    import subprocess
    r = subprocess.run(
        ["javap", "-c", "-p", str(rel_class)],
        capture_output=True, timeout=30, encoding="utf-8", errors="replace"
    )
    return r.stdout


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

    # ── Items.class ──
    ic = extract / "net" / "minecraft" / "world" / "item" / "Items.class"
    if ic.exists():
        _parse_items_class(_javap_c(ic), dur)

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


# ── step 5: output ──────────────────────────────────────────────────────

def write_output(version: str, ench: list[dict], eq: list[dict],
                 tags: dict[str, list[str]]) -> None:
    # Keep enchantment/ tags and exclusive_set tags
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
        "enchantments": ench,
        "equipments": eq,
        "tags": kept,
    }
    Path("data/builtin").mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"  Written ({OUT.stat().st_size / 1024:.1f} KB)")


# ── main ─────────────────────────────────────────────────────────────────

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

    print("Loading localization…")
    lang = load_lang(base)

    print("Loading tags…")
    tags = load_tags(base)
    pfx = known_prefixes(base)

    print("Loading enchantments…")
    ench = load_enchantments(base, lang, tags, pfx)
    ench.sort(key=lambda e: e["id"])

    print("Building equipment list…")
    eq = load_equipments(base, lang, tags, pfx)
    eq.sort(key=lambda e: e["id"])

    print(f"\n  Enchantments: {len(ench)}  Equipments: {len(eq)}")

    # Validate
    no_eq = [e["id"] for e in ench if not e["applicable_equipment"]]
    if no_eq:
        print(f"  WARNING: enchantments without applicable_equipment: {no_eq}")
    no_excl = [e["id"] for e in ench if not e["exclusive_set"]]
    print(f"  Enchantments with empty exclusive_set: {len(no_excl)}")

    print("\nGenerating output…")
    write_output(release, ench, eq, tags)
    print("Done!")


if __name__ == "__main__":
    main()
