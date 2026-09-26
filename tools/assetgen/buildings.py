#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 건물 그림(Phase 4-C) — 키트 건물을 "설계도"로 Higgsfield가 다시 그린 그림을 게임 좌표에 맞춘다.
#
#   1) Client2D --script bldsolo:N        → 실행 폴더에 bldsolo_N.png(자홍 바탕 · 건물 하나) + bldsolo_N.txt(카메라 값)
#   2) python3 buildings.py prep <이름> N <클라 폴더>
#        → raw/buildings/<이름>_ref.png(건물만 남긴 참고 그림) + <이름>_ref.json
#   3) Higgsfield gpt_image_2_5(참고 그림 = 설계도, 배경 투명) → raw/buildings/<이름>_hf.png
#   4) python3 buildings.py fit <이름>
#        → Client2D/assets/buildings/<이름>.png(알파로 잘라 가장자리 색 번짐) + buildings.txt 한 줄
#
# 맞추기: 생성 그림은 참고 그림과 구도가 같지만 몇 % 커지거나 밀린다 → 두 그림의 실루엣 끝점(왼쪽 · 오른쪽 · 아래 꼭짓점)으로
# 가로 · 세로 배율과 이동을 따로 구해, 생성 그림의 사각형을 "건물 기초의 남동 모서리" 기준 아이소 px로 적는다.
# 게임(render/Buildings.cpp)은 그 사각형에 그림을 그리고 깊이는 두 벽면의 천막꼴(앞 모서리가 가장 가깝다)로 준다.
#
# buildings.txt 한 줄: <이름> <마을 기준 x0> <마을 기준 y0> <rx> <ry> <rw> <rh>
# ─────────────────────────────────────────────────────────────────────────────
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RAW = os.path.join(os.path.expanduser('~'), '.cache', 'harven-assetgen', 'raw', 'buildings')
OUT = os.path.join(ROOT, 'Client2D', 'assets', 'buildings')
TOWN_X0, TOWN_Y0 = None, None

sys.path.insert(0, HERE)
from process import bleed  # noqa: E402


def town_origin():
    import re
    src = open(os.path.join(ROOT, 'GameServer', 'Data', 'TownMap.h'), encoding='utf-8').read()
    m = re.search(r'kX0 = (\d+), kY0 = (\d+)', src)
    return int(m.group(1)), int(m.group(2))


def building_mask(rgb):
    from scipy import ndimage
    a = rgb.astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mag = (r - g > 60) & (b - g > 60) & (np.abs(r - b) < 90)
    lab, n = ndimage.label(~mag)
    if n == 0:
        raise SystemExit('건물이 없다')
    sizes = ndimage.sum(~mag, lab, range(1, n + 1))
    keep = lab == (int(np.argmax(sizes)) + 1)
    return ndimage.binary_fill_holes(keep)


def extremes(mask):
    ys, xs = np.where(mask)
    xl, xr, yb = xs.min(), xs.max(), ys.max()
    return dict(left=(float(xl), float(ys[xs == xl].mean())), right=(float(xr), float(ys[xs == xr].mean())),
                bottom=(float(xs[ys == yb].mean()), float(yb)), top=float(ys.min()))


def fit_masks(rmask, gmask):
    """rmask(참고) → gmask(생성) 좌표: g = r · s + t. 1/4 크기에서 배율(가로 · 세로 따로)과 이동을 찾아 IoU가 가장 큰 값."""
    k = 4
    def small(m):
        im = Image.fromarray((m * 255).astype(np.uint8))
        return np.asarray(im.resize((max(1, m.shape[1] // k), max(1, m.shape[0] // k)), Image.BILINEAR)) > 127
    r, g = small(rmask), small(gmask)
    gy, gx = np.nonzero(g)
    gc = np.array([gx.mean(), gy.mean()])
    ry0, rx0 = np.nonzero(r)
    rh, rw = r.shape
    # 첫 추정: 넓이 비의 제곱근(같은 배율) → 가로 · 세로 따로 ±12%
    base = np.sqrt(g.sum() / max(1, r.sum()))
    def iou_for(sxv, syv, dx=0.0, dy=0.0):
        W, H = max(1, int(round(rw * sxv))), max(1, int(round(rh * syv)))
        rs = np.asarray(Image.fromarray((r * 255).astype(np.uint8)).resize((W, H), Image.BILINEAR)) > 127
        ys, xs = np.nonzero(rs)
        if len(xs) == 0:
            return -1, 0, 0
        ox = gc[0] - xs.mean() + dx
        oy = gc[1] - ys.mean() + dy
        canvas = np.zeros_like(g)
        xi, yi = np.round(xs + ox).astype(int), np.round(ys + oy).astype(int)
        ok = (xi >= 0) & (xi < g.shape[1]) & (yi >= 0) & (yi < g.shape[0])
        canvas[yi[ok], xi[ok]] = True
        inter = (canvas & g).sum()
        union = (canvas | g).sum()
        return inter / max(1, union), ox, oy
    best = (-1, 0, 0, 0, 0)
    for fx in np.arange(0.88, 1.121, 0.02):
        for fy in np.arange(0.88, 1.121, 0.02):
            v, ox, oy = iou_for(base * fx, base * fy)
            if v > best[0]:
                best = (v, base * fx, base * fy, ox, oy)
    v, bsx, bsy, _, _ = best
    for fx in np.arange(-0.012, 0.0121, 0.004):
        for fy in np.arange(-0.012, 0.0121, 0.004):
            for dx in (-1.5, -0.75, 0, 0.75, 1.5):
                for dy in (-1.5, -0.75, 0, 0.75, 1.5):
                    vv, ox, oy = iou_for(bsx * (1 + fx), bsy * (1 + fy), dx, dy)
                    if vv > best[0]:
                        best = (vv, bsx * (1 + fx), bsy * (1 + fy), ox, oy)
    v, ssx, ssy, ox, oy = best
    # 작은 그림(1/k) 좌표 → 원래 좌표: g = r · s + t, t = ox · k (배율은 그대로)
    return ssx, ssy, ox * k, oy * k, v


def prep(name, idx, client):
    os.makedirs(RAW, exist_ok=True)
    png = os.path.join(client, 'bldsolo_%d.png' % idx)
    txt = os.path.join(client, 'bldsolo_%d.txt' % idx)
    cam = {}
    for line in open(txt, encoding='utf-8'):
        k, *v = line.split()
        cam[k] = [float(x) for x in v]
    im = Image.open(png).convert('RGB')
    rgb = np.asarray(im)
    keep = building_mask(rgb)
    out = rgb.copy()
    out[~keep] = (255, 0, 255)
    ys, xs = np.where(keep)
    pad = 36
    box = [int(max(0, xs.min() - pad)), int(max(0, ys.min() - pad)), int(min(rgb.shape[1], xs.max() + pad + 1)), int(min(rgb.shape[0], ys.max() + pad + 1))]
    Image.fromarray(out).crop(box).save(os.path.join(RAW, name + '_ref.png'))
    ref = dict(index=idx, box=box, cam=cam, ext=extremes(keep[box[1]:box[3], box[0]:box[2]]))
    json.dump(ref, open(os.path.join(RAW, name + '_ref.json'), 'w'), indent=1)
    print('ref', name, box, 'building', cam['building'])


def fit(name):
    ref = json.load(open(os.path.join(RAW, name + '_ref.json')))
    gen = Image.open(os.path.join(RAW, name + '_hf.png')).convert('RGBA')
    ga = np.asarray(gen)[..., 3] > 128
    ge = extremes(ga)
    re_ = ref['ext']
    # 참고 → 생성: gen = ref · (sx, sy) + (tx, ty). 두 실루엣이 가장 많이 겹치는(IoU) 배율 · 이동을 찾는다.
    # (끝점만으로 맞추면 한쪽 그림에만 있는 차양 · 상자 · 굴뚝이 끝점을 바꿔 세로 배율이 크게 틀어진다)
    rm = np.asarray(Image.open(os.path.join(RAW, name + '_ref.png')).convert('RGB'))
    rmask = building_mask(rm)
    sx, sy, tx, ty, iou = fit_masks(rmask, ga)
    print('fit %s: sx=%.4f sy=%.4f tx=%.1f ty=%.1f — 겹침(IoU) %.3f' % (name, sx, sy, tx, ty, iou))
    # 생성 그림의 알파 사각형만 남긴다
    ys, xs = np.where(ga)
    pad = 6   # 투명 여백(선형 필터가 반대쪽 가장자리를 섞지 않게 — 게임은 가장자리 고정으로도 읽는다)
    gx0, gy0 = max(0, int(xs.min()) - pad), max(0, int(ys.min()) - pad)
    gx1, gy1 = min(gen.width, int(xs.max()) + 1 + pad), min(gen.height, int(ys.max()) + 1 + pad)
    crop = gen.crop((gx0, gy0, gx1, gy1))

    cam = ref['cam']
    box = ref['box']
    zoom = cam['zoom'][0]
    tgx, tgy = cam['target']
    ofx, ofy = cam['offset']
    lift = cam['lift'][0]

    def to_iso(gx, gy):
        # 생성 px → 참고 px → 화면 px → 아이소 px(층 올림 전)
        rx, ry = (gx - tx) / sx, (gy - ty) / sy
        scx, scy = box[0] + rx, box[1] + ry
        return (scx - ofx) / zoom + tgx, (scy - ofy) / zoom + tgy + lift
    L, T = to_iso(gx0, gy0)
    R, B = to_iso(gx1, gy1)
    bx0, by0, bx1, by1, lv = (int(v) for v in cam['building'])
    cx, cy = bx1 + 0.55, by1 + 0.55
    corner = ((cx - cy) * 32.0, (cx + cy) * 16.0)
    rx, ry, rw, rh = L - corner[0], T - corner[1], R - L, B - T
    os.makedirs(OUT, exist_ok=True)
    # 게임에서 1.4배 줌까지 또렷하도록 — 아이소 px당 1.6텍셀을 넘으면 줄인다(메모리)
    scale = min(1.0, 1.6 * rw / crop.width)
    if scale < 1.0:
        crop = crop.resize((int(crop.width * scale), int(crop.height * scale)), Image.LANCZOS)
    bleed(crop).save(os.path.join(OUT, name + '.png'), optimize=True)
    X0, Y0 = town_origin()
    line = '%s %d %d %.1f %.1f %.1f %.1f' % (name, bx0 - X0, by0 - Y0, rx, ry, rw, rh)
    path = os.path.join(OUT, 'buildings.txt')
    lines = [l for l in (open(path, encoding='utf-8').read().splitlines() if os.path.exists(path) else [])
             if l.strip() and not l.startswith(name + ' ')]
    if not lines or not lines[0].startswith('#'):
        lines.insert(0, '# 이름 마을기준x0 y0 | 그림 사각형(건물 기초 남동 모서리 기준 아이소 px): rx ry rw rh — tools/assetgen/buildings.py fit')
    lines.append(line)
    open(path, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print(line, '→', crop.size)


if __name__ == '__main__':
    if len(sys.argv) >= 5 and sys.argv[1] == 'prep':
        prep(sys.argv[2], int(sys.argv[3]), sys.argv[4])
    elif len(sys.argv) >= 3 and sys.argv[1] == 'fit':
        fit(sys.argv[2])
    else:
        print(__doc__ or 'buildings.py prep <이름> <N> <클라 폴더> | fit <이름>')
