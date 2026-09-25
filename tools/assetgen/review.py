# -*- coding: utf-8 -*-
"""처리 결과 검토용 한 장(발 위치에 빨간 점, 기준 높이 선). python3 review.py <sprites 폴더> <stem> <출력.jpg> [접두사]"""
import sys, os
from PIL import Image, ImageDraw
d, stem, out = sys.argv[1], sys.argv[2], sys.argv[3]
prefix = sys.argv[4] if len(sys.argv) > 4 else ""
pages = {}
rows = []
for line in open(os.path.join(d, stem + ".txt"), encoding="utf-8"):
    if line.startswith("#"): continue
    n, pg, x, y, w, h, ax, ay, ref = line.split()
    if prefix and not n.startswith(prefix): continue
    rows.append((n, int(pg), int(x), int(y), int(w), int(h), float(ax), float(ay), float(ref)))
cell = 170
cols = 8
H = ((len(rows) + cols - 1) // cols) * (cell + 16)
sheet = Image.new("RGB", (cols * cell, max(H, 1)), (58, 66, 58))
dr = ImageDraw.Draw(sheet)
for i, (n, pg, x, y, w, h, ax, ay, ref) in enumerate(rows):
    if pg not in pages:
        pages[pg] = Image.open(os.path.join(d, f"{stem}_{pg}.png")).convert("RGBA")
    spr = pages[pg].crop((x, y, x + w, y + h))
    k = (cell - 20) / max(ref * 1.3, w, h)
    spr = spr.resize((max(1, int(w * k)), max(1, int(h * k))))
    ox = (i % cols) * cell + cell // 2
    oy = (i // cols) * (cell + 16) + cell - 6
    sheet.paste(spr, (int(ox - ax * k), int(oy - ay * k)), spr)
    dr.ellipse((ox - 3, oy - 3, ox + 3, oy + 3), fill=(255, 40, 40))
    dr.line((ox - 20, oy - ref * k, ox + 20, oy - ref * k), fill=(255, 220, 90))
    dr.text(((i % cols) * cell + 4, (i // cols) * (cell + 16) + cell), n[-16:], fill=(230, 230, 230))
sheet.save(out, quality=85)
print(len(rows))
