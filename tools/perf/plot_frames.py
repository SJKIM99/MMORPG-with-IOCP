#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 프레임 그래프(Phase 6) — tools/sandbox/phase6.sh crowd · crowd0 · fight가 남긴 --perf-csv를 한 장에.
#
#   python3 tools/perf/plot_frames.py <phase6 출력 폴더> [출력 png]
#
# 칸 셋(같은 세로 눈금): 마을 — 봇 없이 · 마을 — 플레이어 30 + NPC 33 · 대규모 전투(몬스터 30 · 세 사람 동시 각성기).
# 선 둘: CPU 시간(갱신 + 그리기 명령 제출 — 샌드박스 소프트웨어 렌더링에서 비교하는 값) · 프레임 시간(전체).
# 색은 DESIGN_SYSTEM 토큰(새벽빛 · 옛길) — 바탕은 창 바탕(kSurface0). 16.7ms(60fps) · 33.3ms(30fps) 기준선.
# ─────────────────────────────────────────────────────────────────────────────
import csv
import os
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
FONT = os.path.join(ROOT, 'Client2D', 'assets', 'fonts', 'NanumSquareRoundB.ttf')
if os.path.exists(FONT):
    font_manager.fontManager.addfont(FONT)
    plt.rcParams['font.family'] = font_manager.FontProperties(fname=FONT).get_name()
plt.rcParams['axes.unicode_minus'] = False

BG, PANEL, LINE, TEXT, DIM, MUTE = '#111519', '#171C21', '#2D353D', '#CFC8BB', '#8E887D', '#605C55'
CPU, FRAME, SHADE = '#D9A55B', '#6FB5AC', '#684D2A'


def load(path, phases):
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path) as f:
        for r in csv.DictReader(f):
            try:
                ph = int(r['phase'])
            except (KeyError, ValueError):
                continue
            if ph in phases:
                rows.append({k: float(v) for k, v in r.items()})
    if rows:
        t0 = rows[0]['t']
        for r in rows:
            r['t'] -= t0
    return rows


def stats(v):
    if not v:
        return 0, 0, 0
    s = sorted(v)
    return sum(v) / len(v), s[min(len(s) - 1, int(len(s) * 0.95))], s[-1]


def main():
    d = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(d, 'perf_frames.png')
    panels = [
        ('마을 — 봇 없이', load(os.path.join(d, 'crowd0.csv'), {1}), None),
        ('마을 — 플레이어 30 · NPC 33', load(os.path.join(d, 'crowd.csv'), {1}), None),
        ('대규모 전투 — 몬스터 30 · 동시 각성기 셋', load(os.path.join(d, 'fight.csv'), {1, 2, 3}), 2),
    ]
    panels = [p for p in panels if p[1]]
    if not panels:
        print('CSV가 없다:', d)
        return 1
    # 세로 눈금: 가장 느린 칸의 99% 값 위로 조금(드문 튐 하나가 눈금을 늘리지 않게)
    def p99(v):
        s = sorted(v)
        return s[min(len(s) - 1, int(len(s) * 0.99))]
    ymax = max(p99([max(r['frame_ms'], r['work_ms']) for r in rows]) for _, rows, _ in panels)
    ymax = max(40.0, ymax * 1.15)
    fig, axes = plt.subplots(1, len(panels), figsize=(5.2 * len(panels), 4.0), dpi=130, sharey=True)
    if len(panels) == 1:
        axes = [axes]
    fig.patch.set_facecolor(BG)
    for ax, (title, rows, shade_phase) in zip(axes, panels):
        ax.set_facecolor(PANEL)
        t = [r['t'] for r in rows]
        cpu = [r['work_ms'] for r in rows]
        frm = [r['frame_ms'] for r in rows]
        if shade_phase is not None:
            ph = [r for r in rows if int(r['phase']) == shade_phase]
            if ph:
                ax.axvspan(ph[0]['t'], ph[-1]['t'], color=SHADE, alpha=0.35, lw=0)
                ax.text((ph[0]['t'] + ph[-1]['t']) / 2, ymax * 0.06, '각성기 4초', color=TEXT, ha='center', va='bottom', fontsize=8.5)
        for y in (16.7, 33.3):
            ax.axhline(y, color=MUTE, lw=1, ls=(0, (4, 3)))
        ax.text(t[-1], 33.3 + ymax * 0.012, '60 · 30fps(16.7 · 33.3ms)', color=DIM, ha='right', va='bottom', fontsize=7.5)
        ax.plot(t, frm, color=FRAME, lw=1.2, alpha=0.9, label='프레임 시간(전체)')
        ax.plot(t, cpu, color=CPU, lw=2.0, label='CPU 시간(갱신 + 그리기 명령)')
        a_cpu, p_cpu, m_cpu = stats(cpu)
        a_frm, p_frm, _ = stats(frm)
        players = int(max(r['players'] for r in rows))
        npcs = int(max(r['npcs'] for r in rows))
        mons = int(max(r['monsters'] for r in rows))
        who = '플레이어 %d · NPC %d' % (players, npcs) + (' · 몬스터 %d' % mons if mons else '')
        ax.set_title(title, color=TEXT, fontsize=10.5, loc='left', pad=8)
        ax.text(0.0, -0.3, 'CPU 평균 %.1f · 95%% %.1f · 최대 %.1f ms\n프레임 평균 %.1f · 95%% %.1f ms · %s' % (a_cpu, p_cpu, m_cpu, a_frm, p_frm, who),
                transform=ax.transAxes, color=DIM, fontsize=8, va='top')
        ax.set_ylim(0, ymax)
        ax.set_xlim(0, t[-1])
        ax.set_xlabel('초', color=DIM, fontsize=8.5)
        ax.tick_params(colors=DIM, labelsize=8)
        for s in ax.spines.values():
            s.set_color(LINE)
        ax.grid(axis='y', color=LINE, lw=0.6)
    axes[0].set_ylabel('ms / 프레임', color=DIM, fontsize=8.5)
    handles, labels = axes[-1].get_legend_handles_labels()
    leg = fig.legend(handles, labels, loc='upper right', ncol=2, fontsize=8.5, frameon=False, bbox_to_anchor=(0.995, 0.995))
    for txt in leg.get_texts():
        txt.set_color(TEXT)
    fig.suptitle('프레임 그래프 — 샌드박스(2코어 · 소프트웨어 렌더링 · 1600×900)', color=TEXT, fontsize=11.5, x=0.01, ha='left')
    fig.tight_layout(rect=(0, 0.03, 1, 0.93))
    fig.savefig(out, facecolor=BG)
    print('→', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
