#!/usr/bin/env python3
"""Regenerate the 16x16 item-icon sources for the Web GUI sprite sheet.

Sources:
- res/vanilla/assets/minecraft/textures/item/<id>.png (the vanilla resource
  pack textures) for every equipment id in data/builtin/vanilla.json plus
  minecraft:enchanted_book.
- Texture-name remaps for ids whose texture lives under another name/path:
    crossbow     → item/crossbow_standby.png  (the held crossbow sprite)
    turtle_shell → item/turtle_helmet.png     (legacy id, same item texture)
- shield is a 3D-model item with no item/ texture and a model-UV entity
  texture — it gets a dedicated 2D silhouette icon (SPECIAL) whose colors
  are sampled from entity/shield/shield_base_nopattern.png.
- leather armor icons are part-composited + tinted (see leather_icon): the
  base layer is a grey leather shape that gets the default dye tint
  0xA06540 applied, then the pre-tinted brown overlay layer is composited
  on top — a single flat item/ texture would render grey and untinted.
- misc has no real texture (placeholder category id in vanilla.json) — it
  gets a generated placeholder so the picker rows stay visually consistent.

Pipeline: this script writes the per-id sources to assets/item_icons/, then
runs scripts/gen_sprite.py which aggregates them into the single sprite
sheet gui/frontend/vendor/icons/sprite.png + the tile index sprite.js.
The sheet is the only icon asset embedded into besq-gui (GUI_ASSET_NAMES +
kAssets); the frontend crops tiles from it via sprite.js (background
position / canvas source rect). Individual per-id PNGs are never shipped.

Run from the repo root:  uv run python scripts/gen_frontend_icons.py
"""
import json
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
VANILLA = ROOT / "data" / "builtin" / "vanilla.json"
TEX_ITEM = ROOT / "res" / "vanilla" / "assets" / "minecraft" / "textures" / "item"
TEX_ENTITY = ROOT / "res" / "vanilla" / "assets" / "minecraft" / "textures" / "entity"
OUT = ROOT / "assets" / "item_icons"
SIZE = 32  # sprite tile 分辨率（16 → 32：盾牌等 3D 渲染图标在高分下不再糊）

REMAP = {
    "crossbow": ("item", "crossbow_standby"),
    "turtle_shell": ("item", "turtle_helmet"),
}
PLACEHOLDER = {"misc"}  # 无真实材质（vanilla.json 中的占位类别 id）

# 有专门 2D 绘制函数的 id（3D 模型物品，直接铺 UV 纹理很难看）
SPECIAL = {"shield": "shield_icon"}

# 皮革盔甲：材质是部件合成 + 调色的（见 leather_icon），不能直接取单张贴图
LEATHER = {"leather_helmet", "leather_chestplate", "leather_leggings", "leather_boots"}
LEATHER_TINT = (160, 101, 64)  # 默认染色 0xA06540（无 display.color 时的皮革棕）

RENDERER = ROOT / "scripts" / "render_item_3d.py"
SHIELD_MODEL = ROOT / "assets" / "models" / "shield.json"
TEX_ROOT = ROOT / "res" / "vanilla" / "assets" / "minecraft" / "textures"


def placeholder() -> Image.Image:
    """Neutral 16x16 tile: dark surface + border + accent slash."""
    img = Image.new("RGBA", (SIZE, SIZE), (58, 54, 47, 255))
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, SIZE - 1, SIZE - 1), outline=(82, 76, 65, 255))
    d.line((4, 11, 11, 4), fill=(46, 154, 208, 255), width=2)
    return img


RENDER_HI = 512  # 3D 渲染源图分辨率（高分辨率渲染 → 平滑缩小，非硬边缘）


def shield_icon() -> Image.Image:
    """盾牌 2D 图标：3D 渲染（scripts/render_item_3d.py + assets/models/shield.json）。

    盾牌是 3D 模型：无 item/ 纹理，entity 纹理按模型 UV 展开（含把手/背面），
    直接平铺成 2D 图标效果很差；由独立渲染引擎把 base 材质按 display
    变换（旋转+正交投影）组装成图标。原始盾牌无 creeper 图案。

    流程：先渲染 RENDER_HI 高分辨率源图（软边缘），BOX 平滑缩小到 16×16
    tile；高分辨率源图同时存为 assets/item_icons/shield_3d.png 资产。
    """
    import subprocess
    import tempfile

    fd, out = tempfile.mkstemp(suffix=".png")
    os.close(fd)
    try:
        subprocess.run(
            [sys.executable, str(RENDERER),
             "--model", str(SHIELD_MODEL),
             "--textures", str(TEX_ROOT),
             "--out", out, "--size", str(RENDER_HI), "--supersample", "2"],
            check=True,
        )
        hi = Image.open(out).convert("RGBA")
        hi.save(OUT.parent / "shield_3d.png")  # 高分辨率渲染源图（assets/ 顶层，不进 sprite）
        return hi.resize((SIZE, SIZE), Image.BOX)
    finally:
        os.unlink(out)


def leather_icon(id_: str) -> Image.Image:
    """皮革盔甲图标 = 部件合成 + 调色（与 MC 1.9+ item 模型一致）。

    models/item/leather_<part>.json 有两层：layer0 = leather_<part>.png
    （灰度皮革形状），layer1 = leather_<part>_overlay.png（预染成棕色的
    装饰层，chestplate 无装饰 = 全透明）。ItemRenderer 只对 layer0 施加
    物品染色（tint index 0）：逐像素 RGB × tint/255（四舍五入，从 overlay
    像素与染色结果同色系可证）；layer1 不染色直接 alpha-over 叠加。
    """
    base = Image.open(TEX_ITEM / f"{id_}.png").convert("RGBA")
    overlay = Image.open(TEX_ITEM / f"{id_}_overlay.png").convert("RGBA")
    r, g, b = LEATHER_TINT
    px = base.load()
    for y in range(base.height):
        for x in range(base.width):
            pr, pg, pb, pa = px[x, y]
            if pa:
                px[x, y] = ((pr * r + 127) // 255,
                            (pg * g + 127) // 255,
                            (pb * b + 127) // 255, pa)
    return Image.alpha_composite(base, overlay)


def load(id_: str) -> Image.Image:
    if id_ in PLACEHOLDER:
        return placeholder()
    if id_ in SPECIAL:
        return globals()[SPECIAL[id_]]()
    if id_ in LEATHER:
        return leather_icon(id_)
    kind, name = REMAP.get(id_, ("item", id_))
    base = TEX_ENTITY if kind == "entity" else TEX_ITEM
    return Image.open(base / f"{name}.png").convert("RGBA")


def main() -> None:
    vanilla = json.loads(VANILLA.read_text(encoding="utf-8"))
    ids = sorted({e["id"] for e in vanilla["equipments"]} | {"enchanted_book"})
    OUT.mkdir(parents=True, exist_ok=True)
    for id_ in ids:
        img = load(id_)
        if img.size != (SIZE, SIZE):
            # MC 材质是硬边缘像素风，任何重采样必须 NEAREST（无过渡/融合）
            img = img.resize((SIZE, SIZE), Image.NEAREST)
        img.save(OUT / f"{id_}.png")
    print(f"wrote {len(ids)} icons to {OUT}")
    # 聚合为 sprite sheet + 前端索引（同一来源顺序，保证 tile 与 id 同步）
    subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "gen_sprite.py")], check=True
    )


if __name__ == "__main__":
    main()
