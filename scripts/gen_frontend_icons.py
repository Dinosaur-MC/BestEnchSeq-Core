#!/usr/bin/env python3
"""Generate gui/frontend/vendor/icons/ — 16x16 item icons for the Web GUI.

Sources:
- res/vanilla/assets/minecraft/textures/item/<id>.png (the vanilla resource
  pack textures) for every equipment id in data/builtin/vanilla.json plus
  minecraft:enchanted_book.
- Texture-name remaps for ids whose item texture lives under another name:
    crossbow     → crossbow_standby.png  (the held crossbow sprite)
    turtle_shell → turtle_helmet.png     (legacy id, same item texture)
- shield / misc have no item texture — they get a generated placeholder so
  the picker rows stay visually consistent.

The PNGs are committed (git) and embedded into besq-gui via GUI_ASSET_NAMES
+ kAssets; the frontend serves them at /public/vendor/icons/<id>.png with an
onerror-hide fallback for non-vanilla (modded) ids.

Run from the repo root:  uv run python scripts/gen_frontend_icons.py
"""
import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
VANILLA = ROOT / "data" / "builtin" / "vanilla.json"
TEX = ROOT / "res" / "vanilla" / "assets" / "minecraft" / "textures" / "item"
OUT = ROOT / "gui" / "frontend" / "vendor" / "icons"
SIZE = 16

REMAP = {
    "crossbow": "crossbow_standby",
    "turtle_shell": "turtle_helmet",
}
PLACEHOLDER = {"shield", "misc"}  # no item texture in the vanilla pack


def placeholder() -> Image.Image:
    """Neutral 16x16 tile: dark surface + border + accent slash."""
    img = Image.new("RGBA", (SIZE, SIZE), (58, 54, 47, 255))
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, SIZE - 1, SIZE - 1), outline=(82, 76, 65, 255))
    d.line((4, 11, 11, 4), fill=(46, 154, 208, 255), width=2)
    return img


def load(id_: str) -> Image.Image:
    if id_ in PLACEHOLDER:
        return placeholder()
    name = REMAP.get(id_, id_)
    return Image.open(TEX / f"{name}.png").convert("RGBA")


def main() -> None:
    vanilla = json.loads(VANILLA.read_text(encoding="utf-8"))
    ids = sorted({e["id"] for e in vanilla["equipments"]} | {"enchanted_book"})
    OUT.mkdir(parents=True, exist_ok=True)
    for id_ in ids:
        img = load(id_)
        if img.size != (SIZE, SIZE):
            img = img.resize((SIZE, SIZE), Image.LANCZOS)
        img.save(OUT / f"{id_}.png")
    print(f"wrote {len(ids)} icons to {OUT}")


if __name__ == "__main__":
    main()
