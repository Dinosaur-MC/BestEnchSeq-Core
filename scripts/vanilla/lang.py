"""Minecraft language file resource download, extraction and export.

Downloads language files from Mojang's official resource server (or
falls back to the extracted client jar), filters for project-relevant
keys (items, enchantments, effects), and writes structured JSON files
consumable by the project i18n system.

Acquisition flow
────────────────
  version_manifest_v2.json
         ↓
  version detail JSON  ──→  assetIndex.url
         ↓
  Asset Index JSON  ──→  objects["minecraft/lang/<locale>.json"].hash
         ↓
  resources.download.minecraft.net/<hash[:2]>/<hash>
         ↓
  language JSON  ──→  filter relevant keys  ──→  data/i18n/minecraft/<locale>.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

# Allow both ``python scripts/vanilla/lang.py`` and ``python -m scripts.vanilla.lang``
_SCRIPTS = Path(__file__).resolve().parent.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from vanilla import meta
from vanilla.lang_config import RELEVANT_PREFIXES, locale_display


# ── paths ─────────────────────────────────────────────────────────────────

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent                  # scripts/../ = design/
DATA_DIR = PROJECT / "data" / "i18n" / "minecraft"
CACHE_DIR = meta.RES_DIR / "lang_cache"
JAR_LANG_DIR = meta.RES_DIR / "vanilla" / "assets" / "minecraft" / "lang"

# Mojang resource server URL template
RESOURCE_TPL = "https://resources.download.minecraft.net/{hd}/{hash}"


# ── asset index scanning ─────────────────────────────────────────────────

def list_lang_objects(asset_index: dict) -> dict[str, dict]:
    """Scan asset index for ``minecraft/lang/<locale>.json`` entries.

    Returns  ``{locale: {hash, size}}``.
    """
    objects = asset_index.get("objects", {})
    found: dict[str, dict] = {}
    prefix = "minecraft/lang/"
    for path, info in objects.items():
        if path.startswith(prefix) and path.endswith(".json"):
            locale = path[len(prefix):-5]
            found[locale] = info
    return found


# ── download / load ──────────────────────────────────────────────────────

def download_lang(locale: str, obj: dict, *, force: bool = False) -> dict:
    """Download (or load from cache) a single language file.

    Returns the parsed JSON dict.
    """
    cache_path = CACHE_DIR / f"{locale}.json"
    if not force and cache_path.exists():
        return json.loads(cache_path.read_bytes())

    hash_val = obj["hash"]
    url = RESOURCE_TPL.format(hd=hash_val[:2], hash=hash_val)
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    meta.download_file(url, cache_path)
    print(f"    Downloaded ({obj.get('size', '?')} bytes)")
    return json.loads(cache_path.read_bytes())


def load_lang_from_jar(locale: str, *, force: bool = False) -> dict | None:
    """Load a language file previously extracted from the client jar.

    Serves as a fallback for locales (e.g. ``en_us``) that aren't in the
    asset index because they are the bundled base language.

    Returns the parsed JSON dict, or ``None`` if unavailable.
    """
    src = JAR_LANG_DIR / f"{locale}.json"
    if not src.exists():
        return None

    cache_path = CACHE_DIR / f"{locale}.json"
    if not force and cache_path.exists():
        return json.loads(cache_path.read_bytes())

    data = json.loads(src.read_bytes())
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    cache_path.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
    print(f"    Extracted from jar ({len(data)} keys)")
    return data


# ── key extraction ────────────────────────────────────────────────────────

def extract_relevant(data: dict[str, str], *,
                     prefixes: tuple[str, ...] = RELEVANT_PREFIXES,
                     extra_patterns: list[re.Pattern] | None = None) -> dict[str, str]:
    """Keep only keys matching a relevant prefix or extra regex pattern."""
    result: dict[str, str] = {}
    for key, value in data.items():
        if not isinstance(key, str) or not isinstance(value, str):
            continue
        if key.startswith(prefixes):
            result[key] = value
            continue
        if extra_patterns:
            for pat in extra_patterns:
                if pat.search(key):
                    result[key] = value
                    break
    return result


# ── output ────────────────────────────────────────────────────────────────

def write_output(locale: str, extracted: dict[str, str],
                 source_version: str) -> Path:
    """Write extracted translations to ``DATA_DIR/<locale_display>.json``.

    Format matches the project i18n convention::

        {"language": "en_US", "minecraft_locale": "en_us",
         "strings": {"key": "value", ...}}
    """
    display = locale_display(locale)
    out_path = DATA_DIR / f"{display}.json"
    DATA_DIR.mkdir(parents=True, exist_ok=True)

    doc = {
        "language": display,
        "minecraft_locale": locale,
        "source_version": source_version,
        "strings": extracted,
    }
    out_path.write_text(
        json.dumps(doc, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    return out_path


# ── programmatic API ──────────────────────────────────────────────────────

def run(*, locales: str = "en_us,zh_cn",
        version: str | None = None,
        existing_only: bool = False,
        cached_index: bool = False,
        force: bool = False,
        include_extra: list[str] | None = None,
        quiet: bool = False) -> list[str]:
    """Download, extract and export language files.

    Parameters
    ----------
    locales : str
        Comma-separated locale list, or ``"all"``.  Default ``"en_us,zh_cn"``.
    version : str or None
        Minecraft version (default: latest release).
    existing_only : bool
        Re-process cached files only (no network).
    cached_index : bool
        Re-use a previously cached asset index.
    force : bool
        Re-download even cached language files.
    include_extra : list of str, optional
        Additional key regex patterns to include.
    quiet : bool
        Suppress informational output (still prints errors).

    Returns list of written output paths.
    """
    out = []
    _print = lambda *a, **kw: None if quiet else print(*a, **kw)

    version_id = meta.resolve_version(version, meta.VERSION_MANIFEST_V2)

    # asset index
    index: dict | None = None
    if cached_index:
        index = meta.get_cached_asset_index()
    if index is None:
        index = meta.get_asset_index(version_id, meta.VERSION_MANIFEST_V2)
        if cached_index:
            meta.cache_asset_index(index)

    lang_objects = list_lang_objects(index)
    _print(f"  Found {len(lang_objects)} language files in asset index")

    # resolve locale list
    if locales.strip().lower() == "all":
        requested = sorted(lang_objects.keys())
    else:
        requested = [loc.strip() for loc in locales.split(",") if loc.strip()]
    if not requested:
        raise ValueError(f"No locales specified: {locales!r}")

    # compile extra patterns
    extra_patterns: list[re.Pattern] = []
    for pat_str in (include_extra or []):
        try:
            extra_patterns.append(re.compile(pat_str))
        except re.error as e:
            raise ValueError(f"Invalid regex '{pat_str}': {e}") from e

    for locale in requested:
        _print(f"\n[{locale}]")

        # locate source
        if locale in lang_objects:
            obj = lang_objects[locale]
            from_jar = False
        else:
            _print("    (not in asset index, trying jar extraction…)")
            from_jar = True

        # load
        if from_jar:
            data = load_lang_from_jar(locale, force=force)
            if data is None:
                _print("    [SKIP] Not in asset index and no jar extraction available")
                continue
        elif not existing_only:
            try:
                data = download_lang(locale, obj, force=force)
            except Exception as e:
                _print(f"    [FAIL] Download failed: {e}")
                continue
        else:
            cache_path = CACHE_DIR / f"{locale}.json"
            if not cache_path.exists():
                _print("    [SKIP] Not cached")
                continue
            data = json.loads(cache_path.read_bytes())
            _print(f"    Loaded from cache ({len(data)} keys)")

        # extract
        extracted = extract_relevant(data, extra_patterns=extra_patterns)
        if not extracted:
            _print("    [WARN] No relevant keys found!")
        else:
            _print(f"    Extracted {len(extracted)} / {len(data)} keys")

        # write
        out_path = write_output(locale, extracted, version_id)
        _print(f"    → {out_path}")
        out.append(str(out_path))

    return out


# ── standalone CLI ────────────────────────────────────────────────────────

def build_parser() -> argparse.ArgumentParser:
    import argparse
    p = argparse.ArgumentParser(
        description="Download & extract Minecraft language file resources",
    )
    p.add_argument("--locales", "-l", default="en_us,zh_cn",
                    help="Comma-separated locales (or 'all'). Default: en_us,zh_cn")
    p.add_argument("--version", "-V",
                    help="Minecraft version (default: latest release)")
    p.add_argument("--list-locales", action="store_true",
                    help="Print all available locales and exit")
    p.add_argument("--existing-only", "-E", action="store_true",
                    help="Re-process only already-cached lang files")
    p.add_argument("--cached-index", action="store_true",
                    help="Re-use a previously cached asset index")
    p.add_argument("--force", "-f", action="store_true",
                    help="Re-download even cached language files")
    p.add_argument("--include-extra", metavar="REGEX", action="append", default=[],
                    help="Additional key pattern to include (regex, may repeat)")
    return p


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    # quick list mode
    if args.list_locales:
        version_id = meta.resolve_version(args.version, meta.VERSION_MANIFEST_V2)
        index = meta.get_asset_index(version_id, meta.VERSION_MANIFEST_V2)
        lang_objects = list_lang_objects(index)
        print(f"\nAvailable locales for {version_id} ({len(lang_objects)}):")
        for loc in sorted(lang_objects):
            print(f"  {loc}")
        return

    run(locales=args.locales,
        version=args.version,
        existing_only=args.existing_only,
        cached_index=args.cached_index,
        force=args.force,
        include_extra=args.include_extra)


if __name__ == "__main__":
    main()
