#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 이동 기록(Client2D --trace-move out.csv) 분석 — Phase 1 완료 기준을 숫자로 잰다.
#
#   analyze_trace.py stats  <csv> [--remote]           속도 분포 · 멈칫(칸 경계 속도 저하) · 방향 전환 · 카메라
#   analyze_trace.py plot   <out.png> <label=csv> ...  시간-속도 그래프(전/후 비교, 같은 축)
#
# 열: t, lx,ly(논리) · vx,vy(화면에 그린 위치) · rx,ry(다른 플레이어 하나) · camx,camy,zoom · face,rface
#     (+ 새 이동이면 heading, sector, flip, … — 없으면 건너뛴다)
# 멈칫: 달리는 중(앞뒤 프레임이 중앙값의 50% 이상)인데 한 프레임 속도가 중앙값의 85% 밑으로 떨어진 것.
# ─────────────────────────────────────────────────────────────────────────────
import csv
import math
import statistics
import sys

NOMINAL = 1000.0 / 280.0   # 이동속도 100%의 칸/초(MoveRule::kBaseStepMs)


def load(path):
    with open(path, newline='') as f:
        return list(csv.DictReader(f))


def speeds(rows, kx, ky, skip_far=50.0):
    out = []
    for a, b in zip(rows, rows[1:]):
        dt = float(b['t']) - float(a['t'])
        if dt <= 0:
            continue
        ax, ay, bx, by = float(a[kx]), float(a[ky]), float(b[kx]), float(b[ky])
        if ax < 0 or bx < 0:
            continue
        s = math.hypot(bx - ax, by - ay) / dt
        if s > skip_far:
            continue
        out.append((float(b['t']), s))
    return out


def dips(sp, med, low=0.85):
    res = []
    for i in range(1, len(sp) - 1):
        if sp[i - 1][1] > 0.5 * med and sp[i + 1][1] > 0.5 * med and sp[i][1] < low * med:
            res.append(sp[i])
    return res


def speed_stats(rows, kx, ky, t0):
    sp = speeds(rows, kx, ky)
    moving = sorted(s for _, s in sp if s > 0.5)
    if not moving:
        return None
    med = statistics.median(moving)
    d = dips(sp, med)
    return {
        'frames': len(sp), 'moving': len(moving), 'median': med,
        'p5': moving[len(moving) // 20], 'p95': moving[len(moving) * 19 // 20],
        'stdev_pct': 100.0 * statistics.pstdev(moving) / med,
        'dips': len(d), 'dip_times': ['%.2f' % (t - t0) for t, _ in d[:30]],
    }


def facing_changes(rows, col='face'):
    if col not in rows[0]:
        return None
    ev = []
    prev = None
    t0 = float(rows[0]['t'])
    for r in rows:
        v = r[col]
        if v in ('', '-1'):
            continue
        if prev is not None and v != prev:
            ev.append(float(r['t']) - t0)
        prev = v
    quick = sum(1 for a, b in zip(ev, ev[1:]) if b - a < 0.25)
    return {'changes': len(ev), 'within_0.25s': quick}


def camera_stats(rows):
    if 'camx' not in rows[0]:
        return None
    cv = []
    for a, b in zip(rows, rows[1:]):
        dt = float(b['t']) - float(a['t'])
        if dt <= 0:
            continue
        z = float(b['zoom'])
        cv.append(((float(b['camx']) - float(a['camx'])) * z / dt, (float(b['camy']) - float(a['camy'])) * z / dt))
    acc = sorted(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(cv, cv[1:]))
    return {'vel_change_median_px_s': statistics.median(acc), 'vel_change_p95_px_s': acc[len(acc) * 95 // 100], 'vel_change_max_px_s': acc[-1]}


def remote_stats(rows, skip=30):
    sp = speeds(rows, 'rx', 'ry')[skip:]
    if not sp:
        return None
    moving = [s for _, s in sp if s > 0.3]
    if not moving:
        return None
    off = sum(1 for s in moving if abs(s - NOMINAL) / NOMINAL > 0.15)
    stalled = sum(1 for _, s in sp if s <= 0.3)
    return {'frames': len(sp), 'mean': statistics.mean(moving), 'stdev_pct': 100.0 * statistics.pstdev(moving) / NOMINAL,
            'off15_pct': 100.0 * off / len(moving), 'stalled_frames': stalled}


def cmd_stats(path, remote):
    rows = load(path)
    t0 = float(rows[0]['t'])
    print('file', path)
    print('  visual  ', speed_stats(rows, 'vx', 'vy', t0))
    print('  logical ', speed_stats(rows, 'lx', 'ly', t0))
    print('  facing  ', facing_changes(rows))
    if 'sector' in rows[0]:
        print('  sector  ', facing_changes(rows, 'sector'))
    print('  camera  ', camera_stats(rows))
    if remote:
        print('  remote  ', remote_stats(rows))


def cmd_plot(out, pairs, remote=False):
    import os
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    # 한글 라벨: 저장소의 Pretendard(OFL)를 쓴다
    here = os.path.dirname(os.path.abspath(__file__))
    font = os.path.join(here, '..', '..', 'Client2D', 'assets', 'fonts', 'Pretendard-Medium.ttf')
    if os.path.exists(font):
        font_manager.fontManager.addfont(font)
        plt.rcParams['font.family'] = font_manager.FontProperties(fname=font).get_name()
    surface, ink, dim, grid = '#111519', '#EDE7DB', '#8E887D', '#2D353D'
    colors = ['#3987e5', '#d95926']   # 검증된 순서(dataviz 참조 팔레트, 어두운 바탕)
    fig, ax = plt.subplots(figsize=(8.8, 3.0), dpi=110)
    fig.patch.set_facecolor(surface)
    ax.set_facecolor(surface)
    for i, (label, path) in enumerate(pairs):
        rows = load(path)
        t0 = float(rows[0]['t'])
        sp = speeds(rows, 'rx' if remote else 'vx', 'ry' if remote else 'vy')
        xs = [t - t0 for t, _ in sp]
        ys = [s for _, s in sp]
        ax.plot(xs, ys, color=colors[i % 2], lw=1.6, label=label)
        ax.annotate(label, xy=(xs[-1], ys[-1]), xytext=(4, 0), textcoords='offset points', color=ink, fontsize=8, va='center')
    ax.axhline(NOMINAL, color=dim, lw=0.8, ls='--')
    ax.text(0.2, NOMINAL + 0.08, '규칙 속도 %.2f칸/초' % NOMINAL, color=dim, fontsize=8)
    ax.set_ylim(0, NOMINAL * 1.45)
    ax.set_xlabel('초', color=dim, fontsize=8)
    ax.set_ylabel('화면 속도(칸/초)', color=dim, fontsize=8)
    ax.tick_params(colors=dim, labelsize=8)
    for s in ax.spines.values():
        s.set_color(grid)
    ax.grid(True, color=grid, lw=0.6)
    leg = ax.legend(loc='lower right', fontsize=8, frameon=False)
    for t in leg.get_texts():
        t.set_color(ink)
    fig.tight_layout()
    fig.savefig(out, facecolor=surface)
    print('wrote', out)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__ or 'usage: stats <csv> | plot <png> label=csv ...')
        sys.exit(1)
    if sys.argv[1] == 'stats':
        cmd_stats(sys.argv[2], '--remote' in sys.argv)
    elif sys.argv[1] == 'plot':
        remote = '--remote' in sys.argv
        pairs = [a.split('=', 1) for a in sys.argv[3:] if '=' in a]
        cmd_plot(sys.argv[2], pairs, remote)
