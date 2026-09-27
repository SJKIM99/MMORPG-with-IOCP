#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 흰 장면 재기(Phase 6 자기 검토 1) — 녹화 속 보스 둘레가 하얗게 타는 프레임이 얼마나 되는가.
#
#   python3 tools/record/whiteout.py <capture.mp4> [<capture.mp4> …] [--from 8] [--to 70]
#                                    [--box 490,170,300,300] [--names 전,후] [--plot out.png]
#
# 1280×720 녹화(tools/sandbox의 boss3 녹화)에서 화면 가운데 조금 위 300×300 상자(보스가 서는 자리)를 초당 4장 뽑아
# 밝기(0.299R + 0.587G + 0.114B) 235 이상인 픽셀 비율을 잰다. 그 비율이 25%를 넘는 프레임 = "흰 프레임".
# 줄마다: 프레임 수 · 흰 프레임 수(%) · 평균 비율 · 가장 긴 흰 구간(초).
# --plot: 시간에 따른 비율(녹화마다 선 하나, 25% 기준선) — 색은 DESIGN_SYSTEM 바탕 위에서 검사한 두 색(새벽빛 · 청록).
# ─────────────────────────────────────────────────────────────────────────────
import glob
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
FONT = os.path.join(ROOT, 'Client2D', 'assets', 'fonts', 'NanumSquareRoundB.ttf')
FPS = 4
WHITE = 235
SHARE = 0.25


def measure(mp4, t0, t1, box):
    x, y, w, h = box
    d = tempfile.mkdtemp()
    subprocess.run(['nice', '-n', '19', 'ffmpeg', '-v', 'error', '-ss', str(t0), '-to', str(t1), '-i', mp4,
                    '-vf', 'fps=%d,crop=%d:%d:%d:%d' % (FPS, w, h, x, y), os.path.join(d, 'f_%05d.png')], check=True)
    vals = []
    for f in sorted(glob.glob(os.path.join(d, 'f_*.png'))):
        a = np.asarray(Image.open(f).convert('RGB')).astype(np.float32)
        lum = a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
        vals.append(float((lum >= WHITE).mean()))
        os.remove(f)
    os.rmdir(d)
    return np.array(vals)


def runs_of(v):
    runs, cur = [], 0
    for x in v:
        if x > SHARE:
            cur += 1
        elif cur:
            runs.append(cur)
            cur = 0
    if cur:
        runs.append(cur)
    return runs


def main():
    args = sys.argv[1:]
    t0, t1, box, names, plot = 8.0, 70.0, (490, 170, 300, 300), None, None
    files = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--from':
            t0 = float(args[i + 1]); i += 2
        elif a == '--to':
            t1 = float(args[i + 1]); i += 2
        elif a == '--box':
            box = tuple(int(v) for v in args[i + 1].split(',')); i += 2
        elif a == '--names':
            names = args[i + 1].split(','); i += 2
        elif a == '--plot':
            plot = args[i + 1]; i += 2
        else:
            files.append(a); i += 1
    if not files:
        print(__doc__ or 'whiteout.py <capture.mp4> …')
        return 1
    names = names or [os.path.basename(os.path.dirname(os.path.dirname(f))) or f for f in files]
    series = []
    for f, n in zip(files, names):
        v = measure(f, t0, t1, box)
        r = runs_of(v)
        white = int((v > SHARE).sum())
        print('%s: frames=%d white=%d (%.1f%%) mean=%.3f longest=%.2fs' % (n, len(v), white, 100.0 * white / max(1, len(v)), v.mean() if len(v) else 0,
                                                                     (max(r) if r else 0) / FPS))
        series.append((n, v, white, r))
    if plot:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        from matplotlib import font_manager
        if os.path.exists(FONT):
            font_manager.fontManager.addfont(FONT)
            plt.rcParams['font.family'] = font_manager.FontProperties(fname=FONT).get_name()
        BG, PANEL, LINE, TEXT, DIM, MUTE = '#111519', '#171C21', '#2D353D', '#CFC8BB', '#8E887D', '#605C55'
        COLORS = ['#BD7C2C', '#0E9A8A']   # validate_palette.js --mode dark --surface #171C21 → 모두 통과
        fig, ax = plt.subplots(figsize=(10.4, 3.4), dpi=130)
        fig.patch.set_facecolor(BG)
        ax.set_facecolor(PANEL)
        ax.axhline(SHARE * 100, color=MUTE, lw=1, ls=(0, (4, 3)))
        ax.text(t1 - 0.3, SHARE * 100 + 1.2, '흰 프레임 기준 25%', color=DIM, fontsize=8, va='bottom', ha='right')
        for k, (n, v, white, r) in enumerate(series):
            t = [t0 + j / FPS for j in range(len(v))]
            c = COLORS[k % len(COLORS)]
            ax.plot(t, v * 100, color=c, lw=2.0, label='%s — 흰 프레임 %.1f%% · 가장 긴 %.2f초' % (n, 100.0 * white / max(1, len(v)), (max(r) if r else 0) / FPS))
        ax.set_xlim(t0, t1)
        ax.set_ylim(0, max(40.0, min(100.0, max(float(v.max()) for _, v, _, _ in series) * 100 * 1.15)))
        ax.set_xlabel('녹화 초', color=DIM, fontsize=8.5)
        ax.set_ylabel('보스 둘레 흰 픽셀 %', color=DIM, fontsize=8.5)
        ax.tick_params(colors=DIM, labelsize=8)
        for s in ax.spines.values():
            s.set_color(LINE)
        ax.grid(axis='y', color=LINE, lw=0.6)
        leg = ax.legend(loc='upper right', fontsize=8.5, frameon=False)
        for txt in leg.get_texts():
            txt.set_color(TEXT)
        ax.set_title('보스 둘레(300×300) 거의 흰 픽셀(밝기 ≥ 235) 비율 — boss3 같은 70초', color=TEXT, fontsize=10.5, loc='left', pad=8)
        fig.tight_layout()
        fig.savefig(plot, facecolor=BG)
        print('→', plot)
    return 0


if __name__ == '__main__':
    sys.exit(main())
