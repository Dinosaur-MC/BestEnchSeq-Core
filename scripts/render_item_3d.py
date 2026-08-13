#!/usr/bin/env python3
"""render_item_3d.py — 独立软件 3D 物品图标渲染器。

把低模模型按 display 变换（欧拉旋转 + 正交投影 + z-buffer + SSAA 超采样）
渲染成 2D 图标，供图标提取管线处理 3D 模型物品（如盾牌）使用。
渲染形式与 Minecraft 原版一致：连续面（box 六面）→ 每像素 UV 插值 →
NEAREST 采样材质（MC 材质是硬边缘像素图）→ 高分辨率 + 超采样抗锯齿
（原版 3D 渲染不是硬边缘，边缘经 SSAA 平滑）。

用法:
  python3 scripts/render_item_3d.py --model assets/models/shield.json \\
      --textures res/vanilla/assets/minecraft/textures \\
      --out /tmp/shield.png --size 128 [--supersample 4]

模型 JSON schema:
{
  "display": {"rotation": [rx, ry, rz], "scale": 1.0, "translation": [x, y, z]},
  "textures": {"<name>": "entity/shield/xxx.png", ...},
  "elements": [
    {"type": "sprite", "texture": "<name>", "region": [x, y, w, h],
     "unit_size": [uw, uh], "depth": 3.0, "offset": [ox, oy],
     "side": [r,g,b], "side_bright": 1.15, "back": [r,g,b],
     "overlays": [
        {"rect": [x, y, w, h], "color": [r,g,b]},                          # 实色块
        {"texture": "<name>", "region": [x,y,w,h], "scale": 1.5,
         "dest": [x, y], "tint": [r,g,b]}                                  # 模板染色
     ]},
    {"type": "box", "from": [x0,y0,z0], "to": [x1,y1,z1],
     "faces": {                                                            # 六面各取材质区域组装
        "front":  {"texture": "<name>", "region": [x,y,w,h]},              # z 最小面（朝观察者）
        "back":   {"texture": "<name>", "region": [x,y,w,h]},              # z 最大面
        "left":   {"texture": "<name>", "region": [x,y,w,h]},              # x 最小面
        "right":  {"texture": "<name>", "region": [x,y,w,h]},              # x 最大面
        "top":    {"texture": "<name>", "region": [x,y,w,h]},              # y 最大面
        "bottom": {"texture": "<name>", "region": [x,y,w,h]}               # y 最小面
     }},
    {"type": "cube", "from": [x0,y0,z0], "to": [x1,y1,z1],
     "color": [r,g,b], "bright": 1.0}
  ]
}

box 面 region 语义与 Minecraft 的 ModelPart Cube UV 展开一致：region 的
x 方向沿面宽、y 方向沿面高（材质 v 向下，模型 y 向上 → region 顶部 = 模型
顶边）。来自 MC 26.2 反编译 Cube 构造器的 UV 边界（texOffs 起，模型单位）。
模型坐标 +z 远离观察者（MC GUI 相机在 -z，north/z 最小面朝观察者）。

渲染管线：box/cube 元素 → 连续面（四边形）→ display 变换 → 正交投影 →
三角形光栅化（重心坐标 UV 插值 + NEAREST 材质采样 + z-buffer 深度）→
SSAA 超采样画布 → BOX 平均缩小（平滑边缘）。
sprite 元素走体素路径（旧管线保留，用于非 box 模型）。
"""
import argparse
import json
import math
import sys
from pathlib import Path

from PIL import Image

# ── 旋转矩阵（外旋，按 rotation 顺序 Rx → Ry → Rz 施加）──────────────────


def rot_matrix(rx_deg, ry_deg, rz_deg):
    # MC 模型坐标是左手系（x 右、y 上、z 远离观察者），官方 display rotation
    # 按左手系约定（从轴正方向看顺时针为正）。数学右手矩阵取反角度等价。
    rx, ry, rz = (math.radians(-a) for a in (rx_deg, ry_deg, rz_deg))
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    rx_m = ((1, 0, 0), (0, cx, -sx), (0, sx, cx))
    ry_m = ((cy, 0, sy), (0, 1, 0), (-sy, 0, cy))
    rz_m = ((cz, -sz, 0), (sz, cz, 0), (0, 0, 1))

    def mul(a, b):
        return tuple(
            tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3))
            for i in range(3)
        )

    return mul(rz_m, mul(ry_m, rx_m))


def apply(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


# ── 模型加载 ─────────────────────────────────────────────────────────────


def load_textures(root: Path, spec: dict):
    tex = {}
    for name, rel in spec.get("textures", {}).items():
        p = root / rel
        if not p.exists():
            sys.exit(f"error: texture not found: {p}")
        tex[name] = Image.open(p).convert("RGBA")
    return tex


# ── 面构建（box/cube → 连续四边形；sprite → 体素，旧路径）───────────────

# 面名 → (角点模型坐标顺序, UV 顺序) 角点顺序保证三角形法线朝外
# UV: u 沿面宽（region 左→右），v 沿面高（region 顶 = 模型顶边）
_FACES = {
    # front: z 最小层（朝观察者），u 沿 x+，v 从 y+（顶）
    "front":  ([(0, 1, 0), (1, 1, 0), (1, 0, 0), (0, 0, 0)],
               [(0, 0), (1, 0), (1, 1), (0, 1)]),
    "back":   ([(1, 1, 1), (0, 1, 1), (0, 0, 1), (1, 0, 1)],
               [(0, 0), (1, 0), (1, 1), (0, 1)]),
    "left":   ([(0, 1, 1), (0, 1, 0), (0, 0, 0), (0, 0, 1)],
               [(0, 0), (1, 0), (1, 1), (0, 1)]),
    "right":  ([(1, 1, 0), (1, 1, 1), (1, 0, 1), (1, 0, 0)],
               [(0, 0), (1, 0), (1, 1), (0, 1)]),
    "top":    ([(0, 1, 0), (1, 1, 0), (1, 1, 1), (0, 1, 1)],
               [(0, 0), (1, 0), (1, 1), (0, 1)]),
    "bottom": ([(0, 0, 1), (1, 0, 1), (1, 0, 0), (0, 0, 0)],
               [(0, 0), (1, 0), (1, 1), (0, 1)]),
}


def build_polygons(model: dict, tex: dict):
    """元素 → [(corners, uvs, img|color, region|None), ...]。

    corners: 4 个模型坐标 (x,y,z)；uvs: 4 个归一化 (u,v)；
    img/color: 贴图 Image 或纯色 (r,g,b)；region: (x,y,w,h) 材质区域或 None。
    """
    polys = []
    for el in model["elements"]:
        t = el["type"]
        if t in ("box", "cube"):
            fx, fy, fz = (float(v) for v in el["from"])
            tx, ty, tz = (float(v) for v in el["to"])
            w, h, d = tx - fx, ty - fy, tz - fz
            if t == "cube":
                color = tuple(el["color"])
                bright = el.get("bright", 1.0)
                c = tuple(min(255, int(v * bright)) for v in color)
                for name, (cidx, uvidx) in _FACES.items():
                    corners = [
                        (fx + cidx[0] * w, fy + cidx[1] * h, fz + cidx[2] * d)
                        for cidx in cidx
                    ]
                    polys.append((corners, uvidx, c, None))
            else:
                faces = el.get("faces", {})
                for name, (cidx, uvidx) in _FACES.items():
                    fd = faces.get(name)
                    if not fd:
                        continue
                    corners = [
                        (fx + cidx[0] * w, fy + cidx[1] * h, fz + cidx[2] * d)
                        for cidx in cidx
                    ]
                    polys.append((corners, uvidx, tex[fd["texture"]], fd["region"]))
        elif t == "sprite":
            # 体素路径（旧管线）：拆成逐体素多边形太碎，保留原体素化处理
            polys.append(("sprite", el))
        else:
            sys.exit(f"error: unknown element type '{t}'")
    return polys


# ── 三角形光栅化（重心坐标 + z-buffer）───────────────────────────────────


def rasterize_polys(polys, display, canvas, zbuf, frame):
    """光栅化所有多边形到 frame/zbuf（canvas×canvas）。"""
    R = rot_matrix(*display.get("rotation", [0, 0, 0]))
    scale = display.get("scale", 1.0)
    trans = display.get("translation", [0, 0, 0])

    # 变换 + 正交投影 → 屏幕坐标（y 翻转：模型 y 向上、画布 y 向下）
    proj = []
    for poly in polys:
        if poly[0] == "sprite":
            continue  # 体素元素另行处理
        corners, uvs, img, region = poly
        pts = []
        for (x, y, z), (u, v) in zip(corners, uvs):
            p = apply(R, (x, y, z))
            p = (p[0] * scale + trans[0], p[1] * scale + trans[1], p[2] * scale)
            pts.append((p, u, v))
        proj.append((pts, img, region))

    # 包围盒（fit 居中）
    xs = [p[0][0] for pts, _, _ in proj for p in pts]
    ys = [p[0][1] for pts, _, _ in proj for p in pts]
    minx, maxx = min(xs), max(xs)
    miny, maxy = min(ys), max(ys)
    spanx = max(maxx - minx, 1e-6)
    spany = max(maxy - miny, 1e-6)
    margin = canvas * 0.08
    s = min((canvas - 2 * margin) / spanx, (canvas - 2 * margin) / spany)
    ox = (canvas - spanx * s) / 2
    oy = (canvas - spany * s) / 2

    for pts, img, region in proj:
        scr = []
        for (p, u, v) in pts:
            sx = (p[0] - minx) * s + ox
            sy = (maxy - p[1]) * s + oy
            scr.append((sx, sy, p[2], u, v))
        # 背面剔除（法线朝观察者 = z 负方向 → 三角形绕序判断）
        for tri in ((0, 1, 2), (0, 2, 3)):
            _raster_tri(scr[tri[0]], scr[tri[1]], scr[tri[2]],
                        img, region, canvas, zbuf, frame)


def _raster_tri(a, b, c, img, region, canvas, zbuf, frame):
    (ax, ay, az, au, av) = a
    (bx, by, bz, bu, bv) = b
    (cx, cy, cz, cu, cv) = c
    # 双面渲染（不剔除）：侧面/背面在旋转后可能朝观察者（如手柄硬件），
    # z-buffer 保证正确的近覆盖远；剔除会误删露出的背面。
    x0, x1 = int(math.floor(min(ax, bx, cx))), int(math.ceil(max(ax, bx, cx)))
    y0, y1 = int(math.floor(min(ay, by, cy))), int(math.ceil(max(ay, by, cy)))
    x0, y0 = max(x0, 0), max(y0, 0)
    x1, y1 = min(x1, canvas - 1), min(y1, canvas - 1)
    denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if denom == 0:  # 退化三角形（侧面视角投影成线），无面积不画
        return
    denom = 1.0 / denom
    if img is None:
        imgpx = None
    else:
        imgpx = img.load()
    for yy in range(y0, y1 + 1):
        for xx in range(x0, x1 + 1):
            w1 = ((by - cy) * (xx - cx) + (cx - bx) * (yy - cy)) * denom
            w2 = ((cy - ay) * (xx - cx) + (ax - cx) * (yy - cy)) * denom
            w3 = 1.0 - w1 - w2
            if w1 < 0 or w2 < 0 or w3 < 0:
                continue
            z = w1 * az + w2 * bz + w3 * cz
            if z >= zbuf[yy * canvas + xx]:
                continue
            zbuf[yy * canvas + xx] = z
            u = w1 * au + w2 * bu + w3 * cu
            v = w1 * av + w2 * bv + w3 * cv
            if imgpx is not None:
                rx, ry, rw, rh = region
                tx = min(rx + rw - 1, max(rx, int(rx + u * rw)))
                ty = min(ry + rh - 1, max(ry, int(ry + v * rh)))
                col = imgpx[tx, ty]
                if col[3] == 0:
                    continue
                frame[yy * canvas + xx] = col
            else:
                frame[yy * canvas + xx] = img


# ── 渲染 ─────────────────────────────────────────────────────────────────


def render(model: dict, tex: dict, size: int, ss: int) -> Image.Image:
    polys = build_polygons(model, tex)
    display = model.get("display", {})
    canvas = size * ss
    zbuf = [float("inf")] * (canvas * canvas)
    frame = [None] * (canvas * canvas)

    # 面渲染（box/cube）
    if any(p[0] != "sprite" for p in polys):
        rasterize_polys(polys, display, canvas, zbuf, frame)

    # 体素渲染（sprite 元素，旧路径）
    vox = _voxelize(model, tex)
    if vox:
        _raster_voxels(vox, display, canvas, zbuf, frame)

    img = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    px = img.load()
    for i, col in enumerate(frame):
        if col is not None:
            px[i % canvas, i // canvas] = col
    # BOX 平均缩小：canvas = size × ss 整数倍 → 平滑边缘（SSAA 抗锯齿）
    return img.resize((size, size), Image.BOX)


# ── 体素路径（sprite 元素兼容，旧管线）──────────────────────────────────


def _voxelize(model: dict, tex: dict):
    vox = {}
    for el in model["elements"]:
        if el["type"] != "sprite":
            continue
        rx, ry, rw, rh = el["region"]
        depth = el["depth"]
        front = tex[el["texture"]].load()
        base = Image.new("RGBA", (rw, rh), (0, 0, 0, 0))
        base.paste(tex[el["texture"]].crop((rx, ry, rx + rw, ry + rh)))
        bp = base.load()
        for ov in el.get("overlays", []):
            if "rect" in ov:
                qx, qy, qw, qh = ov["rect"]
                for yy in range(qy, qy + qh):
                    for xx in range(qx, qx + qw):
                        if 0 <= xx < rw and 0 <= yy < rh:
                            bp[xx, yy] = tuple(ov["color"]) + (255,)
            else:
                sx, sy, sw, sh = ov["region"]
                st = tex[ov["texture"]].load()
                sc = ov.get("scale", 1.0)
                dx, dy = ov["dest"]
                t = tuple(ov["tint"])
                for syy in range(sh):
                    for sxx in range(sw):
                        if st[sx + sxx, sy + syy][3] == 0:
                            continue
                        x0 = int(dx + sxx * sc)
                        x1 = int(dx + (sxx + 1) * sc)
                        y0 = int(dy + syy * sc)
                        y1 = int(dy + (syy + 1) * sc)
                        for yy in range(y0, y1):
                            for xx in range(x0, x1):
                                if 0 <= xx < rw and 0 <= yy < rh:
                                    bp[xx, yy] = t + (255,)
        side = tuple(el["side"])
        back = tuple(el.get("back", side))
        side_bright = el.get("side_bright", 1.0)
        bright = lambda c: tuple(min(255, int(v * side_bright)) for v in c)
        unit = el.get("unit_size")
        if unit:
            uw, uh = unit
            for uy in range(uh):
                for ux in range(uw):
                    c = bp[int(rx + ux * rw / uw), int(ry + uy * rh / uh)]
                    if c[3] == 0:
                        continue
                    for z in range(int(depth)):
                        color = c[:3] if z == 0 else (
                            bright(side) if z < depth - 1 else back)
                        vox[(ux - uw / 2, uy - uh / 2, z)] = color
        else:
            for y in range(rh):
                for x in range(rw):
                    c = bp[x, y]
                    if c[3] == 0:
                        continue
                    vx, vy = x - rw / 2, y - rh / 2
                    for z in range(int(depth)):
                        color = c[:3] if z == 0 else (
                            bright(side) if z < depth - 1 else back)
                        vox[(round(vx), round(vy), z)] = color
    return vox


def _raster_voxels(vox: dict, display: dict, canvas: int, zbuf, frame):
    R = rot_matrix(*display.get("rotation", [0, 0, 0]))
    scale = display.get("scale", 1.0)
    trans = display.get("translation", [0, 0, 0])
    pts = []
    for (x, y, z), color in vox.items():
        v = apply(R, (x + 0.5, y + 0.5, z + 0.5))
        v = (v[0] * scale + trans[0], v[1] * scale + trans[1], v[2] * scale)
        pts.append((v, color))
    xs = [p[0][0] for p in pts]
    ys = [p[0][1] for p in pts]
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    spanx = max(maxx - minx, 1e-6)
    spany = max(maxy - miny, 1e-6)
    margin = canvas * 0.08
    s = min((canvas - 2 * margin) / spanx, (canvas - 2 * margin) / spany)
    ox = (canvas - spanx * s) / 2
    oy = (canvas - spany * s) / 2
    for (v, color) in pts:
        x0 = math.floor((v[0] - 0.5 - minx) * s + ox)
        x1 = math.ceil((v[0] + 0.5 - minx) * s + ox)
        y0 = math.floor((maxy - v[1] - 0.5) * s + oy)
        y1 = math.ceil((maxy - v[1] + 0.5) * s + oy)
        for yy in range(y0, y1):
            for xx in range(x0, x1):
                if 0 <= xx < canvas and 0 <= yy < canvas:
                    if v[2] < zbuf[yy * canvas + xx]:
                        zbuf[yy * canvas + xx] = v[2]
                        frame[yy * canvas + xx] = color


def main():
    ap = argparse.ArgumentParser(description="software 3D item-icon renderer")
    ap.add_argument("--model", required=True)
    ap.add_argument("--textures", required=True, help="texture root dir")
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int, default=128)
    ap.add_argument("--supersample", type=int, default=4)
    args = ap.parse_args()

    model = json.loads(Path(args.model).read_text(encoding="utf-8"))
    tex = load_textures(Path(args.textures), model)
    img = render(model, tex, args.size, args.supersample)
    img.save(args.out)
    print(f"wrote {args.out} ({args.size}x{args.size})")


if __name__ == "__main__":
    main()
