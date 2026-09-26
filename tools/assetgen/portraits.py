#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 초상화 한 장 그림(격자) → 대화창 초상화 portrait_<NPC id>.jpg (Phase 4-B 새 주민)
#
#   python3 portraits.py <격자 그림> <열> <행> <id...>
#     예: python3 portraits.py ~/.cache/harven-assetgen/raw/portraits/sheet_4b.png 5 2 21 22 23 24 25 26 28 29 30 33
#
# 초상화 열 장을 한 장(2k · 5×2 칸)에 그려 한 번에 1크레딧 — 한 장씩(0.5 × 10)보다 5분의 1.
# 칸 사이 옅은 회색 선(주문 문구)을 밝기 봉우리로 찾아 칸을 자르고, 480×637(3:4)로 맞춘다.
# 얼굴이 칸 위쪽 절반에 있으므로 세로가 남으면 아래를 자르고 가로가 남으면 가운데를 남긴다.
# ─────────────────────────────────────────────────────────────────────────────
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(ROOT, 'Client2D', 'assets', 'art')
W_OUT, H_OUT = 480, 637


def gutters(profile, n, span):
    """profile(가로 또는 세로 평균 밝기)에서 n-1개의 칸 경계(옅은 선) 자리를 찾는다 — 기대 자리 ±6% 안의 가장 밝은 곳."""
    cuts = []
    for k in range(1, n):
        c = span * k / n
        lo, hi = int(c - span * 0.06), int(c + span * 0.06)
        seg = profile[lo:hi]
        cuts.append(lo + int(np.argmax(seg)))
    return cuts


def band(profile, at, limit=24):
    """경계 선의 두께: 봉우리에서 양쪽으로 밝기가 바탕 수준까지 떨어지는 곳."""
    base = np.median(profile)
    peak = profile[at]
    thr = base + (peak - base) * 0.25
    l = at
    while l > 0 and at - l < limit and profile[l - 1] > thr:
        l -= 1
    r = at
    while r < len(profile) - 1 and r - at < limit and profile[r + 1] > thr:
        r += 1
    return l, r


def cells(im, cols, rows):
    a = np.asarray(im.convert('L')).astype(float)
    H, W = a.shape
    colp = a.mean(axis=0)
    rowp = a.mean(axis=1)
    xs = [0] + gutters(colp, cols, W) + [W]
    ys = [0] + gutters(rowp, rows, H) + [H]
    xb = [(0, 0)] + [band(colp, x) for x in xs[1:-1]] + [(W, W)]
    yb = [(0, 0)] + [band(rowp, y) for y in ys[1:-1]] + [(H, H)]
    out = []
    for r in range(rows):
        for c in range(cols):
            x0, x1 = xb[c][1] + 1, xb[c + 1][0]
            y0, y1 = yb[r][1] + 1, yb[r + 1][0]
            out.append((x0, y0, x1, y1))
    return out


def fit_portrait(cell_im, inset=6):
    w, h = cell_im.size
    cell_im = cell_im.crop((inset, inset, w - inset, h - inset))
    w, h = cell_im.size
    aspect = W_OUT / H_OUT
    if w / h > aspect:
        nw = int(round(h * aspect))
        x0 = (w - nw) // 2
        cell_im = cell_im.crop((x0, 0, x0 + nw, h))
    else:
        nh = int(round(w / aspect))
        # 얼굴이 위쪽이라 아래를 더 자른다(위 25% · 아래 75%)
        y0 = int((h - nh) * 0.25)
        cell_im = cell_im.crop((0, y0, w, y0 + nh))
    return cell_im.resize((W_OUT, H_OUT), Image.LANCZOS)


def main():
    if len(sys.argv) < 5:
        print(__doc__ or 'portraits.py <sheet> <cols> <rows> <id...>')
        return
    sheet, cols, rows = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    ids = [int(v) for v in sys.argv[4:]]
    im = Image.open(sheet).convert('RGB')
    boxes = cells(im, cols, rows)
    if len(ids) > len(boxes):
        raise SystemExit('칸이 모자란다')
    os.makedirs(OUT, exist_ok=True)
    review = Image.new('RGB', (W_OUT * cols // 2, H_OUT * rows // 2), (20, 24, 30))
    for i, (npc_id, box) in enumerate(zip(ids, boxes)):
        p = fit_portrait(im.crop(box))
        path = os.path.join(OUT, 'portrait_%d.jpg' % npc_id)
        p.save(path, quality=90, optimize=True)
        review.paste(p.resize((W_OUT // 2, H_OUT // 2), Image.LANCZOS), ((i % cols) * W_OUT // 2, (i // cols) * H_OUT // 2))
        print('portrait_%d.jpg' % npc_id, 'cell', box, '→', os.path.getsize(path) // 1024, 'KB')
    rp = os.path.splitext(sheet)[0] + '_review.jpg'
    review.save(rp, quality=88)
    print('review', rp)


if __name__ == '__main__':
    main()
