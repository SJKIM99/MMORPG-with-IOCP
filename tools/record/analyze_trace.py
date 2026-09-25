#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 이동 기록(Client2D --trace-move out.csv) 분석 — Phase 1 완료 기준을 숫자로 잰다.
#
#   analyze_trace.py stats  <csv> [--remote]           속도 분포 · 멈칫(칸 경계 속도 저하) · 방향 전환 · 카메라
#   analyze_trace.py plot   <out.png> <label=csv> ...  시간-속도 그래프(전/후 비교, 같은 축) [--remote]
#
# 열: t, lx,ly(논리) · vx,vy(화면에 그린 위치) · rx,ry(다른 플레이어 하나) · camx,camy,zoom · face,rface
#     (+ 새 이동이면 heading, sector, turn, dev, guided, rlx,rly, aimx,aimy, lineo·lineu — 없으면 건너뛴다)
#
# 멈칫: 달리는 중(앞뒤 프레임이 중앙값의 50% 이상)인데 한 프레임 속도가 중앙값의 85% 밑으로 떨어진 것.
# 구간(segments): 0.3초 넘게 서 있으면 나눈다. 구간마다 앞뒤 0.4초(출발 · 정지)를 뺀 안정 구간의 속도 · 방향 흔들림.
# 깜빡임(flicker): 방향 그림(face) · 8구역(sector)이 0.25초 안에 바뀌었다가 **원래대로 돌아온** 것(A → B → A).
#   "within_0.25s"는 0.25초 안에 연달아 바뀐 모든 경우(서 있다 뒤돌아 출발할 때 1 → 3 → 4처럼 한쪽으로 도는 것도 센다).
# 원격(--remote): 다른 플레이어가 시야 안에 계속 보이는 구간만, 나타난 뒤 0.6초(시야 진입 스냅 · 첫 걸음 대기)는 뺀다.
#   stall = 앞뒤로 움직이는 중인데(±6프레임 안에 1칸/초 넘음) 0.3칸/초 밑 — 칸에서 섰다 떠나는 끊김.
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
        'frames': len(sp), 'moving': len(moving), 'median': round(med, 3),
        'p5': round(moving[len(moving) // 20], 3), 'p95': round(moving[len(moving) * 19 // 20], 3),
        'stdev_pct': round(100.0 * statistics.pstdev(moving) / med, 2),
        'dips': len(d), 'dip_times': ['%.2f' % (t - t0) for t, _ in d[:30]],
    }


def unwrap(angles):
    out = []
    for a in angles:
        if not out:
            out.append(a)
            continue
        d = a - out[-1]
        while d > math.pi:
            d -= 2 * math.pi
        while d < -math.pi:
            d += 2 * math.pi
        out.append(out[-1] + d)
    return out


def segments(rows, kx='vx', ky='vy'):
    """움직인 구간별 안정 구간 통계(출발 · 정지 0.4초 제외)"""
    t0 = float(rows[0]['t'])
    sp = []
    for a, b in zip(rows, rows[1:]):
        dt = float(b['t']) - float(a['t'])
        if dt <= 0 or float(a[kx]) < 0 or float(b[kx]) < 0:
            continue
        s = math.hypot(float(b[kx]) - float(a[kx]), float(b[ky]) - float(a[ky])) / dt
        if s > 50:
            continue
        sp.append((float(b['t']) - t0, s, b))
    segs, cur, still = [], [], 0
    for x in sp:
        if x[1] > 0.3:
            cur.append(x)
            still = 0
        else:
            still += 1
            if cur and still >= 9:
                segs.append(cur)
                cur = []
    if cur:
        segs.append(cur)
    out = []
    for sg in segs:
        a, b = sg[0][0], sg[-1][0]
        st = [x for x in sg if a + 0.4 < x[0] < b - 0.4]
        if len(st) < 5:
            continue
        s = sorted(x[1] for x in st)
        med = statistics.median(s)
        d = sum(1 for j in range(1, len(sg) - 1) if sg[j - 1][1] > 0.5 * med and sg[j + 1][1] > 0.5 * med and sg[j][1] < 0.85 * med)
        seg = {'from': round(a, 2), 'to': round(b, 2), 'median': round(med, 3), 'p5': round(s[len(s) // 20], 3),
               'p95': round(s[len(s) * 19 // 20], 3), 'stdev_pct': round(100.0 * statistics.pstdev(s) / med, 2), 'dips': d}
        if 'heading' in st[0][2]:
            h = unwrap([float(x[2]['heading']) for x in st])
            seg['heading_range_deg'] = round(math.degrees(max(h) - min(h)), 1)
        if 'dev' in st[0][2]:
            seg['max_dev'] = round(max(float(x[2]['dev']) for x in st), 3)
        out.append(seg)
    return out


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
            ev.append((float(r['t']) - t0, prev, v))
        prev = v
    quick = sum(1 for a, b in zip(ev, ev[1:]) if b[0] - a[0] < 0.25)
    flicker = sum(1 for a, b in zip(ev, ev[1:]) if b[0] - a[0] < 0.25 and b[2] == a[1])   # A → B → A
    return {'changes': len(ev), 'within_0.25s': quick, 'flicker': flicker}


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
    return {'vel_change_median_px_s': round(statistics.median(acc), 1), 'vel_change_p95_px_s': round(acc[len(acc) * 95 // 100], 1),
            'vel_change_max_px_s': round(acc[-1], 1)}


def remote_stats(rows):
    """다른 플레이어: 시야 안에 계속 보이는 구간만, 나타난 뒤 0.6초는 뺀다"""
    t0 = float(rows[0]['t'])
    spans, cur = [], []
    for r in rows:
        if float(r['rx']) < 0:
            if cur:
                spans.append(cur)
                cur = []
        else:
            cur.append(r)
    if cur:
        spans.append(cur)
    sp = []
    for s in spans:
        ts = float(s[0]['t'])
        for a, b in zip(s, s[1:]):
            t = float(b['t'])
            if t - ts < 0.6:
                continue
            dt = t - float(a['t'])
            if dt <= 0:
                continue
            v = math.hypot(float(b['rx']) - float(a['rx']), float(b['ry']) - float(a['ry'])) / dt
            if v > 50:
                continue
            sp.append((t - t0, v))
    if not sp:
        return None
    moving = [v for _, v in sp if v > 0.3]
    if not moving:
        return None
    off = sum(1 for v in moving if abs(v - NOMINAL) / NOMINAL > 0.15)
    stalls = [sp[i][0] for i in range(1, len(sp) - 1)
              if sp[i][1] <= 0.3 and max(x[1] for x in sp[max(0, i - 6):i]) > 1.0 and max(x[1] for x in sp[i + 1:i + 7]) > 1.0]
    return {'frames': len(sp), 'spans': len(spans), 'mean': round(statistics.mean(moving), 3),
            'stdev_pct': round(100.0 * statistics.pstdev(moving) / NOMINAL, 1), 'off15_pct': round(100.0 * off / len(moving), 1),
            'stall_frames': len(stalls), 'stall_times': ['%.2f' % t for t in stalls[:20]]}


def cmd_stats(path, remote):
    rows = load(path)
    t0 = float(rows[0]['t'])
    print('file', path)
    print('  visual   ', speed_stats(rows, 'vx', 'vy', t0))
    print('  logical  ', speed_stats(rows, 'lx', 'ly', t0))
    for s in segments(rows):
        print('  segment  ', s)
    print('  facing   ', facing_changes(rows))
    if 'sector' in rows[0]:
        print('  sector   ', facing_changes(rows, 'sector'))
    print('  camera   ', camera_stats(rows))
    if remote:
        print('  remote   ', remote_stats(rows))
        if 'rface' in rows[0]:
            print('  rfacing  ', facing_changes(rows, 'rface'))


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
        # 직접 라벨: 마지막으로 움직이던 곳(둘 다 0으로 끝나면 겹치므로)
        last = max((k for k, y in enumerate(ys) if y > 0.5), default=len(ys) - 1)
        ax.annotate(label, xy=(xs[last], ys[last]), xytext=(4, 6 if i == 0 else -10), textcoords='offset points', color=ink, fontsize=8, va='center')
    ax.axhline(NOMINAL, color=dim, lw=0.8, ls='--')
    ax.text(0.2, NOMINAL + 0.08, '규칙 속도 %.2f칸/초' % NOMINAL, color=dim, fontsize=8)
    ax.set_ylim(0, NOMINAL * 1.45)
    ax.set_xlabel('초', color=dim, fontsize=8)
    ax.set_ylabel('다른 플레이어 화면 속도(칸/초)' if remote else '화면 속도(칸/초)', color=dim, fontsize=8)
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
        print(__doc__ or 'usage: stats <csv> [--remote] | plot <png> label=csv ... [--remote]')
        sys.exit(1)
    if sys.argv[1] == 'stats':
        cmd_stats(sys.argv[2], '--remote' in sys.argv)
    elif sys.argv[1] == 'plot':
        remote = '--remote' in sys.argv
        pairs = [a.split('=', 1) for a in sys.argv[3:] if '=' in a]
        cmd_plot(sys.argv[2], pairs, remote)
