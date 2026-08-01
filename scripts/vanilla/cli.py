#!/usr/bin/env python3
"""Vanilla data extraction CLI: orchestrate jar, enchantment, and language extraction.

Pipeline
────────
  1. (optional) Download & extract client jar  ── jar.ensure_jar + jar.extract_jar
  2. Extract enchantment / equipment data       ── enchantment pipeline
  3. (optional) Download & export lang files    ── lang.run()

Usage
─────
  python scripts/vanilla/cli.py                          # full extraction
  python scripts/vanilla/cli.py --with-lang zh_cn        # + language export
  python scripts/vanilla/cli.py --lang-only              # language export only
  python scripts/vanilla/cli.py --lang-only --locales all
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Allow both ``python scripts/vanilla/cli.py`` and ``python -m scripts.vanilla.cli``
_SCRIPTS = Path(__file__).resolve().parent.parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from vanilla import jar, enchantment, lang as lang_mod


# ── paths ─────────────────────────────────────────────────────────────────

EXTRACT = jar.EXTRACT
RES_DIR = jar.RES_DIR


# ── enchantment extraction pipeline ───────────────────────────────────────

def run_enchantment_pipeline(release: str) -> None:
    """Run the full enchantment/equipment data extraction pipeline."""
    base = EXTRACT

    jar.check_javap()

    print("Loading localization…")
    lang_data = enchantment.load_lang(base)

    print("Loading tags…")
    tags = enchantment.load_tags(base)
    pfx = enchantment.known_prefixes(base)

    print("Loading enchantability…")
    ench_map = enchantment.load_enchantability_from_source(RES_DIR)
    dur_map = enchantment.load_durability_from_source(RES_DIR)

    print("Loading enchantments…")
    ench = enchantment.load_enchantments(base, lang_data, tags, pfx, ench_map)
    ench.sort(key=lambda e: e["id"])

    print("Post-processing enchantments…")
    enchantment.post_process_enchantments(ench, tags, pfx)

    print("Building equipment list…")
    eq = enchantment.load_equipments(base, lang_data, tags, pfx, dur_map)
    eq.sort(key=lambda e: e["id"])

    print(f"\n  Enchantments: {len(ench)}  Equipments: {len(eq)}")

    print("Collecting categories…")
    cats = enchantment.collect_categories(ench, eq)

    # Validate
    no_eq = [e["id"] for e in ench if not e["supported_items"]]
    if no_eq:
        print(f"  WARNING: enchantments without supported_items: {no_eq}")
    no_excl = [e["id"] for e in ench if not e["exclusive_set"]]
    print(f"  Enchantments with empty exclusive_set: {len(no_excl)}")

    print("\nGenerating output…")
    enchantment.write_output(release, ench, eq, cats, tags, dur_map, ench_map)
    print("Done!")


# ── main entry point ─────────────────────────────────────────────────────

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Extract vanilla Minecraft data (enchantments + languages)",
    )
    # --- enchantment pipeline ---
    p.add_argument("--skip-jar", action="store_true",
                    help="Skip jar download/extract (use existing extraction)")
    p.add_argument("--jar-only", action="store_true",
                    help="Only download & extract the client jar, stop")

    # --- language pipeline ---
    p.add_argument("--with-lang", nargs="?", const="en_us,zh_cn", default=None,
                    help="Also extract language files (default locales: en_us,zh_cn)")
    p.add_argument("--locales", default=None,
                    help="Locales for language extraction (default: en_us,zh_cn)")
    p.add_argument("--lang-only", action="store_true",
                    help="Only extract language files, skip jar/enchantment pipeline")
    p.add_argument("--cached-index", action="store_true",
                    help="Re-use a previously cached asset index")
    p.add_argument("--existing-only", action="store_true",
                    help="Re-process only already-cached lang files")
    p.add_argument("--force", "-f", action="store_true",
                    help="Re-download even cached language files")
    p.add_argument("--list-locales", action="store_true",
                    help="Print available locales and exit")
    return p


def main() -> None:
    parser = build_parser()
    # Use parse_known_args for backward compat with legacy scripts
    args, _ = parser.parse_known_args()

    # ── lang-only mode ──────────────────────────────────────────────────
    if args.lang_only:
        locales = args.locales or args.with_lang or "en_us,zh_cn"
        lang_mod.run(
            locales=locales,
            cached_index=args.cached_index,
            existing_only=args.existing_only,
            force=args.force,
        )
        return

    # ── list locales mode ──────────────────────────────────────────────
    if args.list_locales:
        # Delegate to lang module
        lang_mod.main()
        return

    # ── jar download ────────────────────────────────────────────────────
    if not args.skip_jar:
        if not jar.ensure_jar():
            sys.exit(1)

        release = jar.get_release_from_cache()

        print("Extracting…")
        jar.extract_jar()

    # ── jar-only mode ──────────────────────────────────────────────────
    if args.jar_only:
        print("Jar ready. Done!")
        return

    # ── enchantment pipeline ───────────────────────────────────────────
    if not args.skip_jar:
        # release was set above
        pass
    else:
        release = jar.get_release_from_cache()
        if release == "unknown":
            print("  [WARN] Can't determine version without jar, using 'unknown'")

    run_enchantment_pipeline(release)

    # ── optional language extraction ───────────────────────────────────
    if args.with_lang is not None:
        locales = args.locales or args.with_lang or "en_us,zh_cn"
        print("\n── Extracting language files ──")
        try:
            lang_mod.run(
                locales=locales,
                cached_index=args.cached_index or True,
                existing_only=args.existing_only,
                force=args.force,
                quiet=False,
            )
        except Exception as e:
            print(f"  [WARN] Lang extraction failed: {e}")


if __name__ == "__main__":
    main()
