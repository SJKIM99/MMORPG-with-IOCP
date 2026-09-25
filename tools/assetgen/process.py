# -*- coding: utf-8 -*-
"""
Higgsfield 원본(1024px 동작판 · 소품판 · 아이콘판 · 텍스처) → 게임이 읽는 아틀라스 + 목록.

  python3 process.py <raw 폴더> <Client2D/assets 폴더> [actors props icons terrain walls fx ui 중 일부]

만드는 것
  sprites/actors_N.png + actors.txt   캐릭터 동작(플레이어 · 몬스터 · NPC)
  sprites/props_N.png  + props.txt    소품
  icons/icons.png      + icons.txt    아이콘(128px 칸)
  terrain/ground_atlas.jpg + .txt     바닥 텍스처 20장(지역 15 + 마을 5)을 5x4 칸에(칸마다 32px 감쌈)
  terrain/<key>.jpg                   이음매 없는 벽 · 지붕 · 성벽 텍스처(512px)
  fx/<key>.png                        효과(검은 바탕은 밝기를 알파로)
  ui/<key>.png                        UI 조각

목록 한 줄:  이름 아틀라스번호 x y w h 발x 발y 기준높이
  · 발(anchor) = 그 동작의 발바닥 가운데(스프라이트 안 px) — 게임은 이 점을 바닥에 놓는다.
  · 기준높이 = 이 캐릭터의 "서 있는 모습" 높이(px). 게임은 월드 높이 / 기준높이로 모든 동작을 같은 배율로 그린다.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))

# ─────────────────────────────────────────────────────────────────────────────
# 공통: 알파 · 성분 · 자르기
# ─────────────────────────────────────────────────────────────────────────────


def load_rgba(path):
    im = Image.open(path).convert("RGBA")
    return im


def clean_alpha(im, floor=10):
    """아주 옅은 알파(생성기의 잔여물)는 지운다."""
    a = np.asarray(im).copy()
    alpha = a[..., 3]
    alpha[alpha < floor] = 0
    a[..., 3] = alpha
    return Image.fromarray(a, "RGBA")


def components(mask, min_px=60):
    """8-이웃 연결 성분(numpy만으로). 반환: [(ys, xs)] 픽셀 좌표 목록."""
    h, w = mask.shape
    labels = np.zeros((h, w), np.int32)
    comps = []
    cur = 0
    ys, xs = np.nonzero(mask)
    for y0, x0 in zip(ys, xs):
        if labels[y0, x0]:
            continue
        cur += 1
        stack = [(y0, x0)]
        labels[y0, x0] = cur
        py, px = [], []
        while stack:
            y, x = stack.pop()
            py.append(y)
            px.append(x)
            for dy in (-1, 0, 1):
                ny = y + dy
                if ny < 0 or ny >= h:
                    continue
                for dx in (-1, 0, 1):
                    nx = x + dx
                    if 0 <= nx < w and mask[ny, nx] and not labels[ny, nx]:
                        labels[ny, nx] = cur
                        stack.append((ny, nx))
        if len(py) >= min_px:
            comps.append((np.array(py), np.array(px)))
    return comps


def _valley(profile, lo, hi):
    """profile[lo:hi]에서 가장 비어 있는(합이 작은) 자리. 같은 값이면 가운데에 가까운 쪽."""
    seg = profile[lo:hi].astype(np.float64)
    mid = (hi - lo) / 2.0
    score = seg + np.abs(np.arange(hi - lo) - mid) * (seg.max() + 1) * 1e-4
    return lo + int(np.argmin(score))


def split_grid(im, cols, rows, dilate=None, min_px=None):
    """격자판을 칸별 그림으로. 칸 경계는 '가장 비어 있는 줄'로 자른다(무기 끝이 옆 칸으로 조금 넘어가도 괜찮다).
    열을 먼저 자르고, 열마다 행을 따로 자른다. 칸 안에서는 작은 부스러기를 지우고 내용만큼 잘라 낸다."""
    a = np.asarray(im)
    alpha = a[..., 3].astype(np.float32)
    H, W = alpha.shape
    colsum = alpha.sum(axis=0)
    xcuts = [0]
    for i in range(1, cols):
        c = W * i / cols
        xcuts.append(_valley(colsum, int(c - W * 0.09), int(c + W * 0.09)))
    xcuts.append(W)
    out = [None] * (cols * rows)
    for ci in range(cols):
        x0, x1 = xcuts[ci], xcuts[ci + 1]
        rowsum = alpha[:, x0:x1].sum(axis=1)
        ycuts = [0]
        for j in range(1, rows):
            c = H * j / rows
            ycuts.append(_valley(rowsum, int(c - H * 0.11), int(c + H * 0.11)))
        ycuts.append(H)
        for rj in range(rows):
            y0, y1 = ycuts[rj], ycuts[rj + 1]
            cell = a[y0:y1, x0:x1].copy()
            m = cell[..., 3] > 24
            if m.sum() < 200:
                continue
            # 부스러기 지우기: 큰 덩어리(가장 큰 것의 3% 이상)만 남긴다(1/4 크기에서 성분 찾기)
            small = Image.fromarray(m.astype(np.uint8) * 255).resize((max(1, m.shape[1] // 4), max(1, m.shape[0] // 4)), Image.BILINEAR)
            sm = np.asarray(small.filter(ImageFilter.MaxFilter(3))) > 40
            comps = components(sm, min_px=2)
            if comps:
                big = max(len(c[0]) for c in comps)
                keep_small = np.zeros(sm.shape, bool)
                for ys, xs in comps:
                    if len(ys) >= big * 0.05:
                        keep_small[ys, xs] = True
                keep = np.asarray(Image.fromarray(keep_small.astype(np.uint8) * 255).resize((m.shape[1], m.shape[0]), Image.NEAREST)) > 0
                keep = np.asarray(Image.fromarray(keep.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(9))) > 0
                cell[~keep, 3] = 0
            sub = Image.fromarray(cell, "RGBA")
            bb = sub.getchannel("A").point(lambda v: 255 if v > 12 else 0).getbbox()
            if bb:
                out[rj * cols + ci] = sub.crop(bb)
    return out


def bleed(im, iterations=6):
    """투명한 곳의 색을 가장자리 색으로 채운다(밉맵 · 선형 필터에서 어두운 테두리가 생기지 않게)."""
    a = np.asarray(im).astype(np.float32)
    rgb = a[..., :3].copy()
    alpha = a[..., 3]
    known = alpha > 8
    for _ in range(iterations):
        if known.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(alpha.shape, np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1), (-1, 1), (1, -1)):
            sh = np.roll(np.roll(known, dy, 0), dx, 1)
            shc = np.roll(np.roll(rgb, dy, 0), dx, 1)
            acc += shc * sh[..., None]
            cnt += sh
        grow = (~known) & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    rgb[~known] = rgb[known].mean(axis=0) if known.any() else 0
    out = np.dstack([rgb, alpha]).clip(0, 255).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def feet_anchor(im, lying=False):
    """발 위치: 가장 아래 불투명 줄 근처(아래 6%)의 가로 가운데."""
    alpha = np.asarray(im)[..., 3] > 40
    ys, xs = np.nonzero(alpha)
    if len(ys) == 0:
        return im.width / 2, im.height
    ymax = ys.max()
    if lying:
        return float((xs.min() + xs.max()) / 2), float(ymax)
    # 아래 12% 띠(앞발 · 뒷발이 모두 들어오게)의 좌우 가운데. 발이 여럿이면 그 사이.
    band = ys >= ymax - max(6, int(im.height * 0.12))
    bx = xs[band]
    return float((bx.min() + bx.max()) / 2), float(ymax - 2)


def core_centroid(im, r):
    """몸통 중심: 알파를 r px 깎아(무기 · 꼬리 · 날개 같은 가는 부분이 사라진다) 남은 덩어리의 무게 중심."""
    a = im.getchannel("A")
    m = np.zeros((im.height, im.width), bool)
    for rr in (r, r // 2, 1):
        if rr >= 1:
            m = np.asarray(a.filter(ImageFilter.MinFilter(2 * rr + 1))) > 128
        if m.sum() >= 80:
            break
    if m.sum() < 80:
        m = np.asarray(a) > 128
    ys, xs = np.nonzero(m)
    if len(xs) == 0:
        return im.width / 2, im.height / 2
    return float(xs.mean()), float(ys.mean())


def aligned_anchors(items, ref_index=0, lying=()):
    """한 캐릭터의 여러 동작 발 위치를 몸통 기준으로 맞춘다.
    기준 동작(서기)은 발 띠 가운데를 쓰고, 그때의 "몸통 중심 - 발" 가로 거리를 다른 동작에도 그대로 적용한다
    → 앞으로 덮치는 동작에서 앞발이 발 위치로 잡혀 몸이 뒤로 튀던 문제가 없어진다."""
    ref = items[ref_index][1]
    r = max(2, int(round(ref.height * 0.05)))
    fx, fy = feet_anchor(ref)
    cx0, _ = core_centroid(ref, r)
    d = cx0 - fx
    out = []
    for i, (name, im) in enumerate(items):
        if i == ref_index:
            out.append((fx, fy))
            continue
        alpha = np.asarray(im)[..., 3] > 40
        ys, xs = np.nonzero(alpha)
        if len(ys) == 0:
            out.append((im.width / 2, float(im.height)))
            continue
        cx, _ = core_centroid(im, r)
        if name in lying:
            out.append((cx, float((ys.min() + ys.max()) / 2)))
        else:
            ax = min(max(cx - d, 0.0), float(im.width))
            out.append((ax, float(ys.max() - 2)))
    return out


def resize(im, s):
    w, h = max(1, round(im.width * s)), max(1, round(im.height * s))
    return im.resize((w, h), Image.LANCZOS)


# ─────────────────────────────────────────────────────────────────────────────
# 아틀라스
# ─────────────────────────────────────────────────────────────────────────────
class Packer:
    def __init__(self, size=2048, pad=6):
        self.size, self.pad = size, pad
        self.pages = []   # [Image]
        self.shelves = []  # per page: [ [y, h, x] ]
        self.items = []   # (name, page, x, y, w, h, ax, ay, ref)

    def _new_page(self):
        self.pages.append(Image.new("RGBA", (self.size, self.size), (0, 0, 0, 0)))
        self.shelves.append([])

    def place(self, im):
        w, h = im.width + self.pad * 2, im.height + self.pad * 2
        if w > self.size or h > self.size:
            raise ValueError("sprite too large")
        for pi in range(len(self.pages)):
            sh = self.shelves[pi]
            for s in sh:
                if h <= s[1] and s[2] + w <= self.size:
                    x, y = s[2], s[0]
                    s[2] += w
                    return pi, x + self.pad, y + self.pad
            top = (sh[-1][0] + sh[-1][1]) if sh else 0
            if top + h <= self.size:
                sh.append([top, h, w])
                return pi, self.pad, top + self.pad
        self._new_page()
        self.shelves[-1].append([0, h, w])
        return len(self.pages) - 1, self.pad, self.pad

    def add(self, name, im, ax, ay, ref):
        pi, x, y = self.place(im)
        self.pages[pi].paste(im, (x, y))   # 투명한 곳의 번진 색(bleed)까지 그대로 — 밉맵 테두리가 어두워지지 않게
        self.items.append((name, pi, x, y, im.width, im.height, ax, ay, ref))

    def add_many(self, entries):
        # 큰 것부터(선반 낭비를 줄인다)
        for e in sorted(entries, key=lambda e: -e[1].height):
            self.add(*e)

    def save(self, outdir, stem):
        os.makedirs(outdir, exist_ok=True)
        for i, p in enumerate(self.pages):
            bb = p.getbbox()
            if bb:   # 마지막 쪽은 쓴 만큼만(2의 거듭제곱 높이로)
                hh = 1
                while hh < bb[3]:
                    hh *= 2
                p = p.crop((0, 0, self.size, min(self.size, hh)))
            p.save(os.path.join(outdir, f"{stem}_{i}.png"), optimize=True)
        with open(os.path.join(outdir, f"{stem}.txt"), "w", encoding="utf-8") as f:
            f.write(f"# name page x y w h anchorX anchorY refHeight — tools/assetgen/process.py\n")
            for it in sorted(self.items):
                f.write("%s %d %d %d %d %d %.1f %.1f %.1f\n" % it)


# ─────────────────────────────────────────────────────────────────────────────
# 캐릭터
# ─────────────────────────────────────────────────────────────────────────────
POSE_NAMES = {
    "act": ["walk1", "walk2", "windup", "strike"],
    "cmb": ["cast", "guard", "hurt", "dead"],
    "back": ["bidle", "bwalk1", "bwalk2", "bstrike"],
    "mon": ["idle", "move", "attack", "hurt"],
    "npc": ["idle", "walk1", "walk2", "talk"],
}
REF_PX = {"pc": 230, "npc": 210, "npc_small": 170, "mon": 210, "boss": 320}

# 동작판마다 크기가 조금씩 다르게 그려진다 — 눈으로 맞춘 보정값(1 = 그대로)
SHEET_FIX = {}


def norm_sheet(poses, names, ref_pose_index, ref_px, lying=()):
    """같은 판의 동작들을 같은 배율로 줄이고 발 위치를 구한다."""
    ref = poses[ref_pose_index]
    s = ref_px / ref.height
    out = []
    for i, (p, n) in enumerate(zip(poses, names)):
        if p is None:
            continue
        q = bleed(resize(p, s))
        ax, ay = feet_anchor(q, lying=(n in lying))
        out.append((n, q, ax, ay))
    return out, s


def process_players(raw, packer):
    for cls in ("knight", "engraver", "hunter"):
        base = clean_alpha(load_rgba(os.path.join(raw, f"player_{cls}_base.png")))
        bb = base.getchannel("A").point(lambda v: 255 if v > 12 else 0).getbbox()
        base = base.crop(bb)
        ref = REF_PX["pc"]
        s_base = ref / base.height
        idle = bleed(resize(base, s_base))
        front = [("idle", idle)]
        # 걷기 판: 걷는 두 동작의 키를 서 있는 모습의 97%로 본다
        act = split_grid(clean_alpha(load_rgba(os.path.join(raw, f"player_{cls}_act.png"))), 2, 2)
        walk_h = np.median([p.height for p in act[:2] if p is not None])
        s_act = ref * 0.97 / walk_h
        for name, p in zip(POSE_NAMES["act"], act):
            if p is not None:
                front.append((name, bleed(resize(p, s_act))))
        back = []
        for sheet in ("cmb", "back"):
            path = os.path.join(raw, f"player_{cls}_{sheet}.png")
            if not os.path.exists(path):
                continue
            poses = split_grid(clean_alpha(load_rgba(path)), 2, 2)
            if sheet == "back":
                bh = np.median([p.height for p in poses[:3] if p is not None])
                s = ref * 0.98 / bh
            else:
                s = s_act * SHEET_FIX.get(f"player_{cls}_{sheet}", 1.0)
            for name, p in zip(POSE_NAMES[sheet], poses):
                if p is None:
                    continue
                (back if sheet == "back" else front).append((name, bleed(resize(p, s))))
        for group in (front, back):
            if not group:
                continue
            for (name, q), (ax, ay) in zip(group, aligned_anchors(group, 0, lying=("dead",))):
                packer.add(f"pc_{cls}_{name}", q, ax, ay, ref)


def process_sheet4(raw, key, prefix, names, ref_px, packer, lying=()):
    path = os.path.join(raw, key + ".png")
    if not os.path.exists(path):
        return False
    poses = split_grid(clean_alpha(load_rgba(path)), 2, 2)
    if poses[0] is None:
        print("  !! no idle pose in", key)
        return False
    s = ref_px / poses[0].height * SHEET_FIX.get(key, 1.0)
    group = []
    for name, p in zip(names, poses):
        if p is None:
            print("  !! missing", key, name)
            continue
        group.append((name, bleed(resize(p, s))))
    for (name, q), (ax, ay) in zip(group, aligned_anchors(group, 0, lying=lying)):
        packer.add(f"{prefix}_{name}", q, ax, ay, ref_px)
    return True


def process_monsters(raw, packer):
    from catalog import MONSTERS
    for mid, key, arch, _ in MONSTERS:
        ref = REF_PX["boss"] if mid >= 29 else REF_PX["mon"]
        process_sheet4(raw, f"mon_{mid:02d}_{key}", f"mon_{mid:02d}", POSE_NAMES["mon"], ref, packer)


def process_npcs(raw, packer):
    from catalog import NPCS, ANIMALS
    for key, pid, _ in NPCS:
        small = key in ("npc_misha", "npc_anka")
        process_sheet4(raw, key, key, POSE_NAMES["npc"], REF_PX["npc_small" if small else "npc"], packer)
    for key, _, _ in ANIMALS:
        process_sheet4(raw, key, key, POSE_NAMES["npc"], 150, packer)


# ─────────────────────────────────────────────────────────────────────────────
# 소품
# ─────────────────────────────────────────────────────────────────────────────
PROP_NAMES = {
    "props_trees_a": ["tree_oak", "tree_forest", "tree_pine", "tree_pine_snow"],
    "props_trees_b": ["tree_dead", "tree_swamp", "tree_burnt", "tree_palm"],
    "props_plants": ["bush", "bush_flower", "reeds", "cactus"],
    "props_rocks_a": ["rock_grey", "rock_moss", "rock_snow", "rock_red"],
    "props_rocks_b": ["rock_sand", "rock_rust", "spire_red", "spike_obsidian"],
    "props_magic": ["mushroom_pink", "mushroom_blue", "crystal_cyan", "crystal_violet"],
    "props_ruins": ["pillar", "grave_round", "grave_cross", "spike_bone"],
    "props_camp": ["palisade", "barrel", "bones", "mound"],
    "props_misc": ["lava_vent", "stump", "snowman", "logs"],
    "props_town": ["lamp", "stall_red", "stall_blue", "fountain"],
}
# 소품의 기준 높이(px) — 게임에서 월드 높이로 맞출 때 쓰는 원래 크기. 큰 것은 크게.
PROP_REF = {"tree": 420, "rock": 200, "spire": 360, "spike": 300, "pillar": 360, "grave": 200, "bush": 190,
            "reeds": 220, "cactus": 300, "mushroom": 260, "crystal": 260, "palisade": 260, "barrel": 150,
            "bones": 130, "mound": 200, "lava": 170, "stump": 140, "snowman": 200, "logs": 140, "lamp": 330,
            "stall": 330, "fountain": 300}


def process_props(raw, packer):
    for key, names in PROP_NAMES.items():
        path = os.path.join(raw, key + ".png")
        if not os.path.exists(path):
            continue
        cells = split_grid(clean_alpha(load_rgba(path)), 2, 2)
        for name, p in zip(names, cells):
            if p is None:
                print("  !! missing prop", key, name)
                continue
            ref = PROP_REF.get(name.split("_")[0], 240)
            q = bleed(resize(p, ref / p.height))
            a = feet_anchor(q)
            packer.add(f"prop_{name}", q, a[0], a[1], ref)


# ─────────────────────────────────────────────────────────────────────────────
# 아이콘
# ─────────────────────────────────────────────────────────────────────────────
def process_icons(raw, outdir):
    from catalog import ICON_SHEETS
    cell = 128
    names = []
    tiles = []
    for key, items in ICON_SHEETS:
        path = os.path.join(raw, key + ".png")
        if not os.path.exists(path):
            continue
        parts = split_grid(clean_alpha(load_rgba(path)), 3, 3, dilate=5)
        for (name, _), p in zip(items, parts):
            if p is None:
                print("  !! missing icon", name)
                continue
            s = (cell - 10) / max(p.width, p.height)
            q = bleed(resize(p, s))
            tile = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
            tile.alpha_composite(q, ((cell - q.width) // 2, (cell - q.height) // 2))
            names.append(name)
            tiles.append(tile)
    cols = 16
    rows = (len(tiles) + cols - 1) // cols
    h = 1
    while h < rows * cell:
        h *= 2
    atlas = Image.new("RGBA", (cols * cell, h), (0, 0, 0, 0))
    for i, t in enumerate(tiles):
        atlas.alpha_composite(t, ((i % cols) * cell, (i // cols) * cell))
    atlas = bleed(atlas, 3)
    os.makedirs(outdir, exist_ok=True)
    atlas.save(os.path.join(outdir, "icons.png"), optimize=True)
    with open(os.path.join(outdir, "icons.txt"), "w", encoding="utf-8") as f:
        f.write(f"# name index (cell {cell}px, {cols} columns) — tools/assetgen/process.py\n")
        for i, n in enumerate(names):
            f.write(f"{n} {i}\n")
    print("icons", len(names))


# ─────────────────────────────────────────────────────────────────────────────
# 텍스처(바닥 · 벽)
# ─────────────────────────────────────────────────────────────────────────────
def make_seamless(im, blend=0.22):
    """반 칸 밀어서 이음매를 가운데로 옮기고, 원본과 섞어 가장자리를 지운다(흔한 방법)."""
    a = np.asarray(im.convert("RGB")).astype(np.float32)
    h, w = a.shape[:2]
    shifted = np.roll(np.roll(a, h // 2, 0), w // 2, 1)
    # 가장자리로 갈수록 shifted(이음매 없는 가장자리)를 쓴다
    yy, xx = np.mgrid[0:h, 0:w]
    dx = np.minimum(xx, w - 1 - xx) / (w * blend)
    dy = np.minimum(yy, h - 1 - yy) / (h * blend)
    t = np.clip(np.minimum(dx, dy), 0, 1)[..., None]
    t = t * t * (3 - 2 * t)
    out = a * t + shifted * (1 - t)
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGB")


def flatten_light(im, radius=48, amount=0.75):
    """큰 밝기 얼룩(생성 그림의 조명 기울기)을 걷어 낸다 — 반복될 때 같은 얼룩이 줄지어 보이는 것을 막는다."""
    a = np.asarray(im.convert("RGB")).astype(np.float32)
    blur = np.asarray(im.convert("RGB").filter(ImageFilter.GaussianBlur(radius))).astype(np.float32)
    mean = a.reshape(-1, 3).mean(axis=0)
    flat = a / np.maximum(blur, 1.0) * mean
    out = a * (1 - amount) + flat * amount
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGB")


def seam_ratio(a, axis):
    """가장자리 이음매의 어긋남 / 안쪽 이웃 픽셀 차이(1 근처면 이미 이음매가 없다)."""
    inner = np.abs(np.diff(a, axis=axis)).mean() + 1e-3
    if axis == 1:
        seam = np.abs(a[:, -1] - a[:, 0]).mean()
    else:
        seam = np.abs(a[-1] - a[0]).mean()
    return seam / inner


def crossfade_axis(a, axis, blend):
    """한 축만 반 칸 밀어 가장자리 띠(blend)에서 섞는다 — 구조가 있는 텍스처는 띠를 좁게."""
    n = a.shape[axis]
    shifted = np.roll(a, n // 2, axis)
    idx = np.arange(n)
    d = np.minimum(idx, n - 1 - idx) / (n * blend)
    t = np.clip(d, 0, 1)
    t = t * t * (3 - 2 * t)
    t = t[None, :, None] if axis == 1 else t[:, None, None]
    return a * t + shifted * (1 - t)


def periodic_crop(a, axis):
    """벽돌 · 판석 · 기와처럼 줄이 있는 텍스처: 반복 주기(자기상관)의 정수배로 잘라 이어지게 한다."""
    n = a.shape[axis]
    prof = a.mean(axis=2).mean(axis=1 - axis)
    prof = prof - prof.mean()
    ac = np.correlate(prof, prof, "full")[n - 1:]
    if ac[0] <= 0:
        return a
    ac = ac / ac[0]
    lo, hi = 40, min(420, n // 2)
    p = lo + int(np.argmax(ac[lo:hi]))
    if ac[p] < 0.25:
        return a
    k = n // p
    L = k * p
    if L < n * 0.6:
        return a
    return a[:, :L] if axis == 1 else a[:L]


def seamless_tex(path, size=512, flatten=True):
    """이음매 없는 텍스처: 이미 이어지면 그대로, 줄무늬 구조면 주기로 자르고, 남은 이음매만 좁게 섞는다."""
    im = Image.open(path).convert("RGB")
    if flatten:
        im = flatten_light(im, amount=0.6)
    a = np.asarray(im).astype(np.float32)
    for axis in (1, 0):
        if seam_ratio(a, axis) < 1.5:
            continue
        c = periodic_crop(a, axis)
        if c.shape != a.shape and seam_ratio(c, axis) < seam_ratio(a, axis) * 0.7:
            a = c
        if seam_ratio(a, axis) >= 1.5:
            a = crossfade_axis(a, axis, 0.1)
    out = Image.fromarray(a.clip(0, 255).astype(np.uint8), "RGB")
    return out.resize((size, size), Image.LANCZOS)


def process_textures(raw, outdir, keys, size=512):
    """벽 · 지붕 · 성벽: 구조(목조 들보 · 줄눈)의 대비를 지키려고 밝기 고르기는 하지 않는다."""
    os.makedirs(outdir, exist_ok=True)
    for key in keys:
        path = os.path.join(raw, key + ".png")
        if not os.path.exists(path):
            continue
        seamless_tex(path, size, flatten=False).save(os.path.join(outdir, key + ".jpg"), quality=90, optimize=True)


def process_ground_atlas(raw, outdir, keys, cols=5, tex=512, pad=32):
    """바닥 텍스처를 한 장에(칸마다 사방 32px를 반대쪽 가장자리로 감싸 둔다 — 셰이더의 반복 · 밉맵용)."""
    os.makedirs(outdir, exist_ok=True)
    cell = tex + pad * 2
    rows = (len(keys) + cols - 1) // cols
    atlas = Image.new("RGB", (cols * cell, rows * cell), (60, 60, 60))
    have = 0
    for i, key in enumerate(keys):
        path = os.path.join(raw, key + ".png")
        if not os.path.exists(path):
            print("  !! missing ground", key)
            continue
        t = seamless_tex(path, tex)
        big = Image.new("RGB", (tex * 3, tex * 3))
        for yy in range(3):
            for xx in range(3):
                big.paste(t, (xx * tex, yy * tex))
        piece = big.crop((tex - pad, tex - pad, tex * 2 + pad, tex * 2 + pad))
        atlas.paste(piece, ((i % cols) * cell, (i // cols) * cell))
        have += 1
    atlas.save(os.path.join(outdir, "ground_atlas.jpg"), quality=88, optimize=True)
    with open(os.path.join(outdir, "ground_atlas.txt"), "w", encoding="utf-8") as f:
        f.write("# cell order (5 columns, 576px cells: 512 + 32px wrap padding) — tools/assetgen/process.py\n")
        for k in keys:
            f.write(k + "\n")
    print("ground atlas", have, "/", len(keys))


# ─────────────────────────────────────────────────────────────────────────────
# 효과 · UI
# ─────────────────────────────────────────────────────────────────────────────
FX_NAMES = {
    "fx_slash": ["slash_arc", "slash_heavy", "thrust", "claw"],
    "fx_impact": ["hit_star", "hit_crit", "hit_ring", "hit_magic"],
    "fx_fire": ["flame", "fireball", "embers", "fire_ring"],
    "fx_ice": ["ice_burst", "frost_crystal", "snowflake", "ice_spikes"],
    "fx_lightning": ["bolt", "bolt_fork", "arc", "spark_ball"],
    "fx_magic": ["rune_circle", "rune_glyph", "orb", "pillar_teal"],
    "fx_holy": ["heal_motes", "pillar_gold", "halo", "dome"],
    "fx_shadow": ["poison_cloud", "venom", "shadow_smoke", "dagger_flash"],
    "fx_smoke": ["dust", "smoke", "dirt", "mist"],
    "fx_loot": ["loot_beam", "glint", "coin_spray", "glow"],
}


def black_to_alpha(im):
    """검은 바탕 효과 → RGBA(밝기 = 알파, 색은 밝게 되돌린다). 더하기 · 알파 혼합 둘 다 쓸 수 있다."""
    a = np.asarray(im.convert("RGB")).astype(np.float32) / 255.0
    lum = a.max(axis=2)
    alpha = np.clip((lum - 0.03) / 0.97, 0, 1)
    rgb = np.where(alpha[..., None] > 1e-3, a / np.maximum(alpha[..., None], 1e-3), 0)
    out = np.dstack([np.clip(rgb, 0, 1) * 255, alpha * 255]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def process_fx(raw, outdir):
    os.makedirs(outdir, exist_ok=True)
    for key, names in FX_NAMES.items():
        path = os.path.join(raw, key + ".png")
        if not os.path.exists(path):
            continue
        im = Image.open(path)
        im = black_to_alpha(im) if key != "fx_smoke" else clean_alpha(im.convert("RGBA"))
        cells = split_grid(im, 2, 2, dilate=15, min_px=80)
        for name, p in zip(names, cells):
            if p is None:
                print("  !! missing fx", key, name)
                continue
            s = 256 / max(p.width, p.height)
            q = resize(p, min(1.0, s))
            # 정사각형 가운데에(회전 · 크기 변화에 유리)
            side = max(q.width, q.height) + 8
            t = Image.new("RGBA", (side, side), (0, 0, 0, 0))
            t.alpha_composite(q, ((side - q.width) // 2, (side - q.height) // 2))
            bleed(t, 3).save(os.path.join(outdir, name + ".png"), optimize=True)


def process_ui(raw, outdir):
    os.makedirs(outdir, exist_ok=True)
    p = os.path.join(raw, "ui_panel_tex.png")
    if os.path.exists(p):
        im = make_seamless(Image.open(p).convert("RGB")).resize((256, 256), Image.LANCZOS)
        im.save(os.path.join(outdir, "panel_tex.png"), optimize=True)
    for key, names in (("ui_ornaments", ["corner", "divider", "plaque", "ring"]),
                       ("ui_cursors", ["cursor_arrow", "cursor_attack", "cursor_pick", "cursor_talk"])):
        p = os.path.join(raw, key + ".png")
        if not os.path.exists(p):
            continue
        cells = split_grid(clean_alpha(load_rgba(p)), 2, 2, dilate=5)
        for name, c in zip(names, cells):
            if c is None:
                continue
            if name.startswith("cursor"):
                c = resize(c, 48 / max(c.width, c.height))
            else:
                c = resize(c, min(1.0, 256 / max(c.width, c.height)))
            bleed(c, 3).save(os.path.join(outdir, name + ".png"), optimize=True)


# ─────────────────────────────────────────────────────────────────────────────
def main():
    raw = sys.argv[1]
    assets = sys.argv[2]
    what = sys.argv[3:] or ["actors", "props", "icons", "terrain", "walls", "fx", "ui"]
    sys.path.insert(0, HERE)
    if "actors" in what:
        pk = Packer()
        process_players(raw, pk)
        process_monsters(raw, pk)
        process_npcs(raw, pk)
        pk.save(os.path.join(assets, "sprites"), "actors")
        print("actors", len(pk.items), "sprites,", len(pk.pages), "pages")
    if "props" in what:
        pk = Packer()
        process_props(raw, pk)
        pk.save(os.path.join(assets, "sprites"), "props")
        print("props", len(pk.items))
    if "icons" in what:
        process_icons(raw, os.path.join(assets, "icons"))
    if "terrain" in what:
        from catalog import TERRAIN
        process_ground_atlas(raw, os.path.join(assets, "terrain"), [k for k, _ in TERRAIN])
    if "walls" in what:
        from catalog import WALL_TEX
        process_textures(raw, os.path.join(assets, "terrain"), [k for k, _ in WALL_TEX], size=512)
    if "fx" in what:
        process_fx(raw, os.path.join(assets, "fx"))
    if "ui" in what:
        process_ui(raw, os.path.join(assets, "ui"))


if __name__ == "__main__":
    main()
