#!/usr/bin/env python3
"""Generate gui/frontend/names_zh.js — vanilla id → 中文名映射.

Sources:
- data/i18n/minecraft/zh_CN.json — official MC zh_cn locale strings
  (enchantment.minecraft.<id>, item.minecraft.<id>), the same file the
  C++ side embeds for tr().
- data/builtin/vanilla.json — the id universe (43 enchantments + 85
  equipments) that must be covered.

Manual additions (ids whose official keys differ or are placeholders):
- turtle_shell: official item key is item.minecraft.turtle_helmet (legacy
  id kept in vanilla.json).
- misc: placeholder category id in vanilla.json (no item texture/name).
- enchanted_book: not in the equipment registry; used by the calculator
  picker.

Run from the repo root:  uv run python scripts/gen_names_zh.py
Regenerate whenever the locale or registry data changes.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCALE = ROOT / "data" / "i18n" / "minecraft" / "zh_CN.json"
VANILLA = ROOT / "data" / "builtin" / "vanilla.json"
OUT = ROOT / "gui" / "frontend" / "names_zh.js"

MANUAL = {
    "turtle_shell": "海龟壳",
    "misc": "杂项",
    "enchanted_book": "附魔书",
}

def main() -> None:
    strings = json.loads(LOCALE.read_text(encoding="utf-8"))["strings"]
    vanilla = json.loads(VANILLA.read_text(encoding="utf-8"))

    ids = {e["id"] for e in vanilla["enchantments"]} | {e["id"] for e in vanilla["equipments"]}
    ids.add("enchanted_book")

    def zh(short_id: str) -> str | None:
        return (
            strings.get(f"enchantment.minecraft.{short_id}")
            or strings.get(f"item.minecraft.{short_id}")
            or MANUAL.get(short_id)
        )

    mapping = {i: zh(i) for i in sorted(ids)}
    missing = {i for i, v in mapping.items() if not v}
    if missing:
        raise SystemExit(f"no zh name for: {sorted(missing)}")

    lines = [
        "// names_zh.js — vanilla id → 中文名映射。",
        "// 生成自 data/i18n/minecraft/zh_CN.json（官方 zh_cn locale，MC 26.2）+",
        "// data/builtin/vanilla.json 的 id 全集；脚本 scripts/gen_names_zh.py 可再生。",
        "// displayName() 是统一入口：zh-CN 下查表，其余语言回落 fallback。",
        "import { langCode } from './i18n.js';",
        "",
        "const NAME_ZH = {",
    ]
    lines += [f"  {json.dumps(k, ensure_ascii=False)}: {json.dumps(v, ensure_ascii=False)}," for k, v in mapping.items()]
    lines += [
        "};",
        "",
        "// 统一显示名入口：id 可为 NSID（minecraft:sharpness）、标签",
        "// （#minecraft:head_armor）或短 id（sharpness）；非 vanilla id 回落",
        "// fallback（后端下发的英文名 / 原文）。",
        "export function displayName(id, fallback) {",
        "  if (langCode() !== 'zh-CN') return fallback || String(id || '');",
        "  const key = String(id || '').replace(/^#?minecraft:/, '').replace(/^#/, '');",
        "  return NAME_ZH[key] || fallback || String(id || '');",
        "}",
        "",
    ]
    # 统一 LF 行尾（.gitattributes: * text=auto eol=lf）。
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"wrote {OUT} with {len(mapping)} entries")

if __name__ == "__main__":
    main()
