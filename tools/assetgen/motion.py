# -*- coding: utf-8 -*-
"""
동작 영상 → 게임 동작 프레임 (Phase 1-D)

그림 한 장(아틀라스의 기준 동작)을 자홍 바탕 캔버스에 놓아 Higgsfield 영상 생성의 시작 · 끝 그림으로 주고,
"제자리에서 달리기" 영상을 받아 한 주기를 N장으로 잘라 아틀라스에 넣는다.

  python3 motion.py compose <이름>          시작 그림(자홍 바탕) → 원본 폴더/<이름>_start.png
  python3 motion.py fetch <이름>            영상 내려받기(motion_jobs.json의 url) → 원본 폴더/<이름>.mp4
  python3 motion.py build [<이름> …]        영상 → 자홍 빼기 → 주기 찾기 → 한 주기 N장 → sprites/motion_0.png + motion.txt
                                            (이름을 주지 않으면 영상이 있는 것 전부. 검토 그림은 원본 폴더/review/)

주문서 motion_jobs.json — 이름마다
  src      기준 동작(actors.txt의 이름). 게임 속 크기 · 발 위치를 이 그림에 맞춘다.
  canvas   시작 그림 크기(정사각형 px) · scale 기준 동작 배율 · foot 캔버스에서 그림 밑동 가운데
  frames   한 주기에서 뽑을 장수 · model/resolution/duration/aspect_ratio/credits/prompt 생성 조건
  media/job/url  올린 시작 그림 · 생성 job · 결과 영상

원본 폴더: ASSETGEN_RAW(기본 ~/.cache/harven-assetgen/raw)/motion — 저장소 밖

가공
  · 자홍 빼기: 키 = min(R, B) − G. 바탕(≈240) → 알파 0, 몸(≤24) → 알파 1. 가장자리는 바탕색을 덜어 낸다(색 번짐 제거).
  · 주기: 1/4 크기 프레임끼리 차이 D[i, j]. 평균 D[i, i+L]이 가장 작은 L(첫 · 끝 8장 제외 — 시작 · 끝 그림으로 끌려가는 구간).
  · 위상: 시작 그림과 닮고(D[0, c]) 한 주기 뒤와도 닮은(D[c, c+L]) c에서 시작 → 0번 프레임 = 기준 동작(걸음 위상 π/2).
  · 발 기준점: 카메라가 고정이므로 한 주기 전체가 같은 바닥점을 쓴다 = 시작 그림의 기준점 + (0번 ↔ c번 알파 평행 이동).
    그래서 공중에 뜬 프레임은 뜬 채로 그려진다(발바닥에 맞춰 누르면 몸이 오르내리는 움직임이 사라진다).
  · 크기: 영상 px × canvas / (영상 폭 × scale) = 아틀라스 px. 기준높이는 src와 같게.
"""
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from process import Packer, bleed, components  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SPRITES = os.path.join(ROOT, "Client2D", "assets", "sprites")
RAW = os.path.join(os.environ.get("ASSETGEN_RAW", os.path.expanduser("~/.cache/harven-assetgen/raw")), "motion")
JOBS = os.path.join(HERE, "motion_jobs.json")
KEYS = {"magenta": (255, 0, 255), "green": (0, 255, 0)}   # 캐릭터에 없는 색을 바탕으로(자홍: 기사 · 각인사, 초록: 붉은 옷의 사냥꾼)


def load_jobs():
    with open(JOBS, encoding="utf-8") as f:
        return json.load(f)


# ─────────────────────────────────────────────────────────────────────────────
# 아틀라스에서 기준 동작 꺼내기
# ─────────────────────────────────────────────────────────────────────────────
def atlas_sprite(name, stem="actors"):
    with open(os.path.join(SPRITES, stem + ".txt"), encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                continue
            p = line.split()
            if len(p) == 9 and p[0] == name:
                page, x, y, w, h = (int(float(v)) for v in p[1:6])
                ax, ay, ref = float(p[6]), float(p[7]), float(p[8])
                im = Image.open(os.path.join(SPRITES, f"{stem}_{page}.png")).convert("RGBA").crop((x, y, x + w, y + h))
                return im, ax, ay, ref
    raise KeyError(name)


def start_layout(job):
    """시작 그림에서 기준 동작이 놓이는 자리: (붙인 왼쪽 위, 배율 적용 크기, 캔버스 속 발 기준점)."""
    im, ax, ay, ref = atlas_sprite(job["src"])
    s = job["scale"]
    w, h = round(im.width * s), round(im.height * s)
    x0, y0 = job["foot"][0] - w // 2, job["foot"][1] - h
    return im, (x0, y0), (w, h), (x0 + ax * s, y0 + ay * s), ref


def compose(name):
    job = load_jobs()[name]
    im, (x0, y0), (w, h), anchor, _ = start_layout(job)
    c = job["canvas"]
    bg = Image.new("RGBA", (c, c), KEYS[job.get("key", "magenta")] + (255,))
    bg.alpha_composite(im.resize((w, h), Image.LANCZOS), (x0, y0))
    os.makedirs(RAW, exist_ok=True)
    out = os.path.join(RAW, name + "_start.png")
    bg.convert("RGB").save(out)
    print(out, "anchor", anchor)


def fetch(name):
    job = load_jobs()[name]
    os.makedirs(RAW, exist_ok=True)
    out = os.path.join(RAW, name + ".mp4")
    if not os.path.exists(out):
        subprocess.run(["curl", "-sSfL", "-o", out, job["url"]], check=True)
    print(out, os.path.getsize(out))


# ─────────────────────────────────────────────────────────────────────────────
# 영상 읽기 · 자홍 빼기
# ─────────────────────────────────────────────────────────────────────────────
def read_video(path):
    wh = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                         "-of", "csv=p=0", path], capture_output=True, text=True, check=True).stdout.strip().split(",")
    w, h = int(wh[0]), int(wh[1])
    raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(-1, h, w, 3)


def keyness(c, kind):
    """바탕색다움: 자홍 = min(R, B) − G, 초록 = G − max(R, B). 바탕 ≈ 240, 몸 ≤ 24."""
    if kind == "green":
        return c[..., 1] - np.maximum(c[..., 0], c[..., 2])
    return np.minimum(c[..., 0], c[..., 2]) - c[..., 1]


def backdrop(frames, kind="magenta", band=6):
    f = frames.astype(np.float32)
    b = np.concatenate([f[:, :band].reshape(-1, 3), f[:, -band:].reshape(-1, 3),
                        f[:, :, :band].reshape(-1, 3), f[:, :, -band:].reshape(-1, 3)])
    K = np.median(b, axis=0)
    kb = float(np.median(keyness(b, kind)))
    return K, kb, kind


def key(frame, bd, kf=24.0, margin=14.0):
    """(몸 색, 알파). 가장자리 픽셀 C = a·F + (1−a)·K 에서 F를 되돌린다. bd = backdrop()."""
    K, kb, kind = bd
    c = frame.astype(np.float32)
    k = keyness(c, kind)
    hi = kb - margin
    a = np.clip((hi - k) / (hi - kf), 0.0, 1.0)
    a[a < 0.03] = 0.0
    fg = (c - (1.0 - a[..., None]) * K) / np.maximum(a, 0.08)[..., None]
    return np.clip(fg, 0, 255), a


def _min3(a):
    p = np.pad(a, 1, mode="edge")
    return np.min([p[dy:dy + a.shape[0], dx:dx + a.shape[1]] for dy in range(3) for dx in range(3)], axis=0)


def _max3(a):
    p = np.pad(a, 1, mode="edge")
    return np.max([p[dy:dy + a.shape[0], dx:dx + a.shape[1]] for dy in range(3) for dx in range(3)], axis=0)


def decontaminate(fg, a, tint, reach=12):
    """영상 압축(색 해상도 절반)이 가는 칼날 · 망토 끝에 자홍/초록 기운을 남긴다 — 세 단계로 지운다.
    1) 자홍↔초록 축((R+B)/2 − G)을 누른다(밝기 유지): 무채색(R ≈ B — 강철 · 칼날)은 ±6 안으로,
       채도 있는 색은 주문서의 tint 범위로(캐릭터 본래 색의 범위 — 없으면 거의 두지 않는다)
    2) 알파를 반 픽셀 조이고(가장자리 테두리 한 겹), 몸에서 떨어진 옅은 빛무리를 지운다
    3) 가장자리 색은 안쪽(불투명) 색을 끌어와 섞는다"""
    fg = fg.copy()
    m = (fg[..., 0] + fg[..., 2]) * 0.5 - fg[..., 1]
    # 무채색(강철 · 칼날: R ≈ B)은 거의 0으로, 채도 있는 색(금테 · 망토: R ≠ B)은 tint 범위까지 둔다
    w = np.clip((np.abs(fg[..., 0] - fg[..., 2]) - 24.0) / 48.0, 0.0, 1.0)
    lo = -6.0 + (tint[0] + 6.0) * w
    hi = 6.0 + (tint[1] - 6.0) * w
    t = (np.clip(m, lo, hi) - m) / 0.852
    fg += t[..., None] * np.array([0.5, -0.352, 0.5], np.float32)
    a = (a + _min3(a)) * 0.5
    # 빛무리(지팡이 끝 빛이 바탕에 번진 옅은 막)는 지운다: 몸(알파 0.7↑)에서 3px 넘게 떨어진 옅은 픽셀(0.5↓)
    body = a > 0.7
    for _ in range(3):
        body = _max3(body.astype(np.float32)) > 0.5
    a = np.where((a < 0.5) & ~body, 0.0, a)
    inner = _min3((a >= 0.92).astype(np.float32)) > 0.5
    ext = fg * inner[..., None]
    known = inner.copy()
    for _ in range(reach):
        acc = np.zeros_like(fg)
        cnt = np.zeros(a.shape, np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1), (-1, 1), (1, -1)):
            sh = np.roll(np.roll(known, dy, 0), dx, 1)
            acc += np.roll(np.roll(ext, dy, 0), dx, 1) * sh[..., None]
            cnt += sh
        grow = (~known) & (cnt > 0)
        ext[grow] = acc[grow] / cnt[grow][:, None]
        known |= grow
    edge = (a > 0) & ~inner & known
    fg[edge] = ext[edge] * 0.75 + fg[edge] * 0.25
    return np.clip(fg, 0, 255), a


def keep_body(a):
    """부스러기(압축 잡티 · 떨어진 점)를 지운다: 1/4 크기에서 가장 큰 덩어리의 5% 이상만."""
    m = a > 0.1
    h, w = m.shape
    sm = np.asarray(Image.fromarray(m.astype(np.uint8) * 255).resize((max(1, w // 4), max(1, h // 4)), Image.BILINEAR)) > 30
    comps = components(sm, min_px=1)
    if not comps:
        return a
    big = max(len(c[0]) for c in comps)
    keep = np.zeros(sm.shape, bool)
    for ys, xs in comps:
        if len(ys) >= big * 0.05:
            keep[ys, xs] = True
    keep = np.asarray(Image.fromarray(keep.astype(np.uint8) * 255).resize((w, h), Image.NEAREST)) > 0
    from PIL import ImageFilter
    keep = np.asarray(Image.fromarray(keep.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(9))) > 0
    return np.where(keep, a, 0.0)


# ─────────────────────────────────────────────────────────────────────────────
# 주기 · 위상
# ─────────────────────────────────────────────────────────────────────────────
def features(frames, bd, size=120):
    out = []
    for fr in frames:
        im = np.asarray(Image.fromarray(fr).resize((size, size), Image.BILINEAR)).astype(np.float32)
        fg, a = key(im, bd)
        out.append(np.concatenate([(fg * a[..., None]).ravel() / 255.0, a.ravel() * 2.0]))
    X = np.stack(out)
    sq = (X * X).sum(1)
    D2 = np.maximum(sq[:, None] + sq[None, :] - 2.0 * X @ X.T, 0.0) / X.shape[1]
    return np.sqrt(D2)


def find_cycle(D, edge=8, lmin=8):
    n = len(D)
    best = None
    for L in range(lmin, (n - 2 * edge) // 2 + 1):
        idx = np.arange(edge, n - edge - L)
        if len(idx) < 4:
            break
        v = float(D[idx, idx + L].mean())
        if best is None or v < best[0]:
            best = (v, L)
    L = best[1]
    cands = [(float(D[0, c] + D[c, c + L]), c) for c in range(edge, n - edge - L)]
    score, c = min(cands)
    return L, c, best[0], float(D[0, c]), float(D[c, c + L])


def shift_between(a0, a1, r=10):
    """a1을 (dx, dy)만큼 옮기면 a0과 가장 잘 겹친다 → a1의 점 = a0의 점 − (dx, dy)."""
    best = None
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            s = np.roll(np.roll(a1, dy, 0), dx, 1)
            v = float(np.abs(s - a0)[r:-r, r:-r].mean())
            if best is None or v < best[0]:
                best = (v, dx, dy)
    return best[1], best[2]


def stride_check(frames, bd, c, L, s_out, world_h, ref):
    """발 미끄러짐 검사: 디딘 발이 한 주기에 뒤로 쓸려 가는 거리(칸)를 잰다 — 게임 보폭(1.9칸)과 같아야 발이 땅에 붙어 보인다.
    가장 낮은 띠(가는 칼 · 지팡이는 9px 열림으로 지운다)의 가로 위치가 프레임마다 뒤(왼쪽)로 가는 속도의 중앙값 × 주기.
    제자리 걸음(발이 뒤로 가지 않음) 영상은 0에 가깝다 → 게임에서 디딘 발이 몸과 함께 미끄러진다."""
    from PIL import ImageFilter
    xs = []
    for i in range(c, c + L + 1):
        _, a = key(frames[i], bd)
        m = Image.fromarray((a > 0.5).astype(np.uint8) * 255)
        m = np.asarray(m.filter(ImageFilter.MinFilter(9)).filter(ImageFilter.MaxFilter(9))) > 128
        ys, xx = np.nonzero(m)
        if len(ys) == 0:
            continue
        band = ys >= ys.max() - 10
        xs.append(float(xx[band].mean()))
    d = np.diff(xs)
    back = [-v for v in d if -30.0 < v < -1.0]
    v = float(np.median(back)) if len(back) >= 3 else 0.0
    tile_x = 32.0 * ref / world_h            # ↘ 한 칸의 가로 성분(아틀라스 px)
    return v * L * s_out / tile_x, len(back)


# ─────────────────────────────────────────────────────────────────────────────
# 만들기
# ─────────────────────────────────────────────────────────────────────────────
def to_sprite(fg, a, anchor, s_out, pad=3):
    """자른 RGBA(아틀라스 배율) + 그 안의 발 기준점."""
    ys, xs = np.nonzero(a > 0.02)
    x0, x1 = max(0, xs.min() - pad), min(a.shape[1], xs.max() + 1 + pad)
    y0, y1 = max(0, ys.min() - pad), min(a.shape[0], ys.max() + 1 + pad)
    rgba = np.dstack([fg, a * 255.0])[y0:y1, x0:x1].clip(0, 255).astype(np.uint8)
    im = Image.fromarray(rgba, "RGBA").convert("RGBa")   # 미리 곱한 알파로 줄여야 가장자리가 어두워지지 않는다
    w, h = max(1, round(im.width * s_out)), max(1, round(im.height * s_out))
    im = im.resize((w, h), Image.LANCZOS).convert("RGBA")
    ax = (anchor[0] - x0) * im.width / (x1 - x0)
    ay = (anchor[1] - y0) * im.height / (y1 - y0)
    return bleed(im), ax, ay


def build_one(name, job, packer, review_dir):
    path = os.path.join(RAW, name + ".mp4")
    frames = read_video(path)
    n, H, W, _ = frames.shape
    bd = backdrop(frames, job.get("key", "magenta"))
    K, kb = bd[0], bd[1]
    D = features(frames, bd)
    L, c, loop, d_start, d_close = find_cycle(D)
    # 한 주기를 고르게 나눌 수 있는 장수(주기의 약수) 중 주문 장수에 가장 가까운 것 — 고르지 않으면 한 장씩 건너뛰어 걸음이 울컥인다
    want = job.get("frames", 12)
    divs = [d for d in range(6, 25) if L % d == 0] or [want]
    N = min(divs, key=lambda d: (abs(d - want), -d))
    pick = [c + int(round(j * L / N)) for j in range(N)]

    _, _, _, anchor_c, ref = start_layout(job)
    v = W / job["canvas"]
    _, a0 = key(frames[0], bd)
    _, ac = key(frames[c], bd)
    dx, dy = shift_between(a0, ac)
    anchor = (anchor_c[0] * v - dx, anchor_c[1] * v - dy)
    s_out = 1.0 / (v * job["scale"])

    out = []
    for j, i in enumerate(pick):
        fg, a = key(frames[i], bd)
        fg, a = decontaminate(fg, keep_body(a), job.get("tint", (-128.0, 128.0)))
        spr, ax, ay = to_sprite(fg, a, anchor, s_out)
        out.append((f"{name}{j:02d}", spr, ax, ay, ref))
    packer.add_many(out)
    stride, nback = stride_check(frames, bd, c, L, s_out, job.get("world_h", 70.0 * 1.16), ref)
    print(f"{name}: {n} frames {W}x{H}, bg {K.round(1)} key {kb:.0f} | period {L} ({L / 24:.2f}s@24) start {c} "
          f"loop {loop:.3f} start~{d_start:.3f} close {d_close:.3f} | shift ({dx},{dy}) anchor ({anchor[0]:.1f},{anchor[1]:.1f}) "
          f"→ {N} frames {pick}")
    ok = abs(stride / 1.9 - 1.0) <= 0.2
    print(f"  발 쓸림 {stride:.2f}칸/주기(게임 보폭 1.9, 뒤로 가는 프레임 {nback}) → {'통과' if ok else '미끄러짐 — 다시 뽑을 것'}")
    if review_dir:
        review(name, out, review_dir)
    return out


def review(name, items, outdir):
    """프레임 줄(발 기준점 빨간 점 · 바닥 선) + 제자리 재생 GIF."""
    os.makedirs(outdir, exist_ok=True)
    left = max(it[2] for it in items) + 10
    right = max(it[1].width - it[2] for it in items) + 10
    cw = int(left + right)
    top = max(it[3] for it in items) + 10
    bot = max(it[1].height - it[3] for it in items) + 10
    ch = int(top + bot)
    sheet = Image.new("RGB", (cw * len(items), ch), (58, 62, 66))
    d = ImageDraw.Draw(sheet)
    gif = []
    for j, (nm, spr, ax, ay, _) in enumerate(items):
        ox, oy = j * cw + int(left), int(top)
        sheet.paste(spr, (int(round(ox - ax)), int(round(oy - ay))), spr)
        d.line([(j * cw, oy), (j * cw + cw - 1, oy)], fill=(120, 160, 120))
        d.ellipse([ox - 3, oy - 3, ox + 3, oy + 3], fill=(230, 40, 40))
        d.text((j * cw + 4, 4), nm[-2:], fill=(255, 255, 255))
        fr = Image.new("RGB", (cw, ch), (58, 62, 66))
        fr.paste(spr, (int(round(left - ax)), int(round(top - ay))), spr)
        ImageDraw.Draw(fr).line([(0, int(top)), (cw, int(top))], fill=(120, 160, 120))
        gif.append(fr)
    sheet.save(os.path.join(outdir, name + "_sheet.png"))
    gif[0].save(os.path.join(outdir, name + ".gif"), save_all=True, append_images=gif[1:], duration=45, loop=0)
    print("review", os.path.join(outdir, name + "_sheet.png"))


def build(names):
    jobs = load_jobs()
    names = names or [k for k in jobs if os.path.exists(os.path.join(RAW, k + ".mp4"))]
    pk = Packer(size=2048, pad=6)
    for nm in names:
        build_one(nm, jobs[nm], pk, os.path.join(RAW, "review"))
    pk.save(SPRITES, "motion")
    lst = os.path.join(SPRITES, "motion.txt")
    with open(lst, encoding="utf-8") as f:
        lines = f.read().splitlines()
    lines[0] = "# name page x y w h anchorX anchorY refHeight — tools/assetgen/motion.py (동작 영상 → 프레임)"
    with open(lst, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("motion", len(pk.items), "sprites,", len(pk.pages), "pages")


def main():
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "compose":
        compose(args[0])
    elif cmd == "fetch":
        fetch(args[0])
    elif cmd == "build":
        build(args)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
