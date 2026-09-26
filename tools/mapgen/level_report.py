#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 레벨 점검(Phase 4-B) — 생성된 공유 헤더 GameServer/Data/TownMap.h를 읽어
#   · 달리기 시간: 끝에서 끝 · 광장에서 주요 시설(클라 길찾기와 같은 규칙 — 대각은 두 옆이 모두 열려야)
#   · 높이 쓰임: 층별 걷는 칸 · 주요 동선이 지나는 층
#   · 랜드마크 시선: 걷는 칸마다 1600×900 화면(기본 줌 1.45 · 가장 먼 줌 0.9)에 랜드마크 꼭대기가 들어오는가
# 를 세고, 설계도 그림 두 장(docs/img/level_denica_map.png · level_denica_section.png)을 그린다.
#
#   python3 tools/mapgen/level_report.py            # 표를 출력하고 그림을 쓴다
#
# 숫자는 docs/LEVEL_DENICA.md가 인용한다 — 마을 원본을 고치면 다시 돌린다.
# ─────────────────────────────────────────────────────────────────────────────
import heapq
import math
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.dirname(__file__))
from gen_town import LEGEND, STAIRS  # noqa: E402

TOWNMAP = os.path.join(ROOT, 'GameServer', 'Data', 'TownMap.h')
IMG = os.path.join(ROOT, 'docs', 'img')

STEP_S = 0.5            # 기본 달리기: 곧은 한 칸 500ms(Phase 1-C), 대각은 √2배
TILE_W, TILE_H, UNIT_Z, LEVEL_Z = 64, 32, 36, 27   # Draw.h IsoPx · kUnitZ · 1층 = 0.75z

# 랜드마크: 이름, 마을 기준 칸(꼭대기가 서는 곳), 바닥에서 꼭대기까지 높이(z 단위 — 4-C 건물 키트의 값과 맞춘다)
LANDMARKS = [
    ('샛별 탑', 43, 57, 7.1),
    ('예배당 첨탑', 60, 5, 6.4),
    ('풍차', 9, 7, 6.6),
    ('북쪽 전망대', 80, 3, 4.6),
    ('결사 회관 깃발', 40, 21, 4.4),
    ('경매장 종루', 21, 37, 5.0),
    ('남문 망루', 35, 70, 3.2),
]


def parse_townmap():
    src = open(TOWNMAP, encoding='utf-8').read()
    m = re.search(r'kX0 = (\d+), kY0 = (\d+), kW = (\d+), kH = (\d+)', src)
    X0, Y0, W, H = map(int, m.groups())

    def grid(name):
        body = re.search(name + r'\[kH\] =\s*\{(.*?)\};', src, re.S).group(1)
        rows = re.findall(r'"([^"]*)"', body)
        assert len(rows) == H, name
        return rows
    ground = grid('kGround')
    level = [[int(c) for c in row] for row in grid('kLevel')]
    buildings = []
    for kind, x0, y0, x1, y1, lv, door, var, sign in re.findall(
            r"\{ BuildingKind::e(\w+), (\d+), (\d+), (\d+), (\d+), (\d+), '(\w)', (\d+), \"([^\"]*)\" \}", src):
        buildings.append(dict(kind=kind, x0=int(x0) - X0, y0=int(y0) - Y0, x1=int(x1) - X0, y1=int(y1) - Y0,
                              level=int(lv), door=door, variant=int(var), sign=sign))
    homes = {int(i): (int(x) - X0, int(y) - Y0) for i, x, y in re.findall(r'\{ (\d+), (\d+), (\d+) \},', src.split('kNpcHomes')[1].split('};')[0])}
    anchors = {}
    for name, x, y in re.findall(r'PointF k(\w+)\{ ([\d.]+)f, ([\d.]+)f \}', src):
        anchors[name] = (float(x) - X0, float(y) - Y0)
    districts = [(n, s, float(x) - X0, float(y) - Y0) for n, s, x, y, z in
                 re.findall(r'\{ "([^"]+)", "([^"]*)", ([\d.]+)f, ([\d.]+)f, ([\d.]+)f \}', src.split('kDistricts')[1].split('};')[0])]
    return dict(X0=X0, Y0=Y0, W=W, H=H, ground=ground, level=level, buildings=buildings, homes=homes,
                anchors=anchors, districts=districts)


def walkable(t, x, y):
    return 0 <= x < t['W'] and 0 <= y < t['H'] and LEGEND.get(t['ground'][y][x], (False,))[0]


def dijkstra(t, sources):
    """모든 걷는 칸까지의 달리기 시간(초). 대각은 두 옆이 모두 열려야(MoveRule::CleanStep · 클라 path::CanStep)."""
    W, H = t['W'], t['H']
    dist = [[math.inf] * W for _ in range(H)]
    pq = []
    for (x, y) in sources:
        if walkable(t, x, y):
            dist[y][x] = 0.0
            pq.append((0.0, x, y))
    heapq.heapify(pq)
    while pq:
        d, x, y = heapq.heappop(pq)
        if d > dist[y][x]:
            continue
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if not dx and not dy:
                    continue
                nx, ny = x + dx, y + dy
                if not walkable(t, nx, ny):
                    continue
                if dx and dy and not (walkable(t, x + dx, y) and walkable(t, x, y + dy)):
                    continue
                nd = d + STEP_S * (math.sqrt(2) if dx and dy else 1.0)
                if nd < dist[ny][nx]:
                    dist[ny][nx] = nd
                    heapq.heappush(pq, (nd, nx, ny))
    return dist


def time_to_reach(dist, x, y, rng):
    """(x, y)에서 체비셰프 rng 칸 안의 걷는 칸 가운데 가장 빠른 곳(NPC와 말하는 거리 = 3)."""
    best = math.inf
    for yy in range(int(y) - rng, int(y) + rng + 1):
        for xx in range(int(x) - rng, int(x) + rng + 1):
            if 0 <= yy < len(dist) and 0 <= xx < len(dist[0]):
                best = min(best, dist[yy][xx])
    return best


def path_back(t, dist, gx, gy):
    """dist를 거슬러 목적지에서 출발점까지의 칸 목록(그림용)."""
    path = [(gx, gy)]
    x, y = gx, gy
    for _ in range(4000):
        if dist[y][x] == 0:
            break
        best = None
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                nx, ny = x + dx, y + dy
                if (dx or dy) and 0 <= nx < t['W'] and 0 <= ny < t['H'] and dist[ny][nx] < dist[y][x]:
                    if dx and dy and not (walkable(t, x + dx, y) and walkable(t, x, y + dy)):
                        continue
                    if best is None or dist[ny][nx] < dist[best[1]][best[0]]:
                        best = (nx, ny)
        if best is None:
            break
        x, y = best
        path.append(best)
    return path[::-1]


def screen(dx, dy, z):
    return (dx - dy) * TILE_W / 2, (dx + dy) * TILE_H / 2 - z * UNIT_Z


def sightlines(t):
    """걷는 칸마다: 1600×900 화면(카메라가 캐릭터 몸 높이에 맞춘다)에 랜드마크 꼭대기가 들어오는 줌별 비율."""
    res = {}
    for zoom in (1.45, 0.9):
        hw, hh = 800 / zoom - 24, 450 / zoom - 24     # 가장자리 24px는 빼고
        seen = 0
        total = 0
        per = [0] * len(LANDMARKS)
        for y in range(t['H']):
            for x in range(t['W']):
                if not walkable(t, x, y):
                    continue
                total += 1
                pz = t['level'][y][x] * LEVEL_Z / UNIT_Z
                any_seen = False
                for i, (name, lx, ly, h) in enumerate(LANDMARKS):
                    # 위쪽 절반(꼭대기에서 높이의 반까지)이 화면에 걸리면 보인다 — 몸통 아래만 걸친 것은 세지 않는다
                    base_z = t['level'][ly][lx] * LEVEL_Z / UNIT_Z
                    sx, top_y = screen(lx - x, ly - y, base_z + h - pz - 0.9)   # 카메라는 몸 가운데(0.9z)를 본다
                    _, mid_y = screen(lx - x, ly - y, base_z + h * 0.5 - pz - 0.9)
                    if abs(sx) < hw and top_y < hh and mid_y > -hh:
                        per[i] += 1
                        any_seen = True
                if any_seen:
                    seen += 1
        res[zoom] = (seen, total, per)
    return res


def fmt_s(v):
    return '—' if v == math.inf else f'{v:.1f}초'


def main():
    t = parse_townmap()
    W, H = t['W'], t['H']
    homes = t['homes']
    lines = []
    out = lines.append

    # ── 층 쓰임 ──
    per_level = [0] * 5
    for y in range(H):
        for x in range(W):
            if walkable(t, x, y):
                per_level[t['level'][y][x]] += 1
    out('## 층별 걷는 칸')
    out('| 층 | 0 | 1 | 2 | 3 | 4 |')
    out('|---|---|---|---|---|---|')
    out('| 칸 | ' + ' | '.join(str(v) for v in per_level) + ' |')

    # ── 끝에서 끝 ──
    gates = {}
    for y in range(H):
        for x in range(W):
            if t['ground'][y][x] == 'G':
                side = 'W' if x == 0 else 'E' if x == W - 1 else 'S' if y == H - 1 else 'N'
                gates.setdefault(side, []).append((x, y))
    out('')
    out('## 끝에서 끝(달리기 · 곧은 한 칸 0.5초)')
    out('| 구간 | 시간 | 곧은 거리 |')
    out('|---|---|---|')
    pairs = [('서문', 'W', '동문', 'E'), ('남문', 'S', '예배당 문', None)]
    temple = next(b for b in t['buildings'] if b['kind'] == 'Temple')
    temple_door = ((temple['x0'] + temple['x1']) // 2, temple['y1'] + 1)
    windmill = next(b for b in t['buildings'] if b['kind'] == 'WindMill')
    windmill_door = ((windmill['x0'] + windmill['x1']) // 2, windmill['y1'] + 1)
    lookout = next(b for b in t['buildings'] if b['kind'] == 'Lookout')
    lookout_door = (lookout['x0'] - 1, (lookout['y0'] + lookout['y1']) // 2)
    for an, a, bn, b in pairs:
        src = gates[a]
        dist = dijkstra(t, src)
        if b:
            tgt = min(dist[y][x] for (x, y) in gates[b])
            ax, ay = src[len(src) // 2]
            bx, by = gates[b][len(gates[b]) // 2]
        else:
            tgt = time_to_reach(dist, *temple_door, 0)
            ax, ay = src[len(src) // 2]
            bx, by = temple_door
        out(f'| {an} → {bn} | {fmt_s(tgt)} | {math.hypot(bx - ax, by - ay):.0f}칸 |')
    # 마을 지름(가장 먼 두 걷는 칸) — 두 번 쓸기 근사
    d0 = dijkstra(t, gates['S'])
    far = max(((d0[y][x], x, y) for y in range(H) for x in range(W) if d0[y][x] < math.inf))
    d1 = dijkstra(t, [(far[1], far[2])])
    far2 = max(((d1[y][x], x, y) for y in range(H) for x in range(W) if d1[y][x] < math.inf))
    out(f'| 가장 먼 두 칸 ({far[1]},{far[2]}) ↔ ({far2[1]},{far2[2]}) | {fmt_s(far2[0])} | — |')

    # ── 광장에서 주요 시설 ──
    px, py = t['anchors']['Plaza']
    dplaza = dijkstra(t, [(int(px), int(py))])
    facilities = [
        ('잡화점 · 잡화상 라다', 1), ('대장간 · 도브란', 2), ('방어구점 · 체도', 23), ('길잡이 마테오', 3),
        ('촌장 고란', 6), ('경매장 · 로렌초', 21), ('결사 회관 · 즈데노', 22), ('여관 · 베스나', 8),
        ('선술집 · 네다', 24), ('훈련장 · 교관 두샨', 27), ('남문 · 경비대장 야로', 4), ('예배당 · 사제 밀레나', 7),
    ]
    out('')
    out('## 광장(분수 앞)에서 주요 시설 — NPC와 말하는 거리(3칸)까지')
    out('| 시설 | 시간 | 층 |')
    out('|---|---|---|')
    fac_times = {}
    for name, nid in facilities:
        hx, hy = homes[nid]
        tt = time_to_reach(dplaza, hx, hy, 3)
        fac_times[nid] = tt
        out(f'| {name} | {fmt_s(tt)} | {t["level"][hy][hx]} |')

    # ── 동선: 남문 → 경비 → 잡화 · 대장간 → 길잡이 → 광장 → 경매장 · 결사 회관 ──
    flow = [('남문', gates['S'][len(gates['S']) // 2]), ('경비대장 야로', homes[4]), ('잡화상 라다', homes[1]),
            ('대장장이 도브란', homes[2]), ('길잡이 마테오', homes[3]), ('분수 광장', (int(px), int(py))),
            ('경매인 로렌초', homes[21]), ('회관지기 즈데노', homes[22])]
    out('')
    out('## 처음 온 사람의 동선(퀘스트 순서) — 구간마다 달리기 시간 · 지나는 층')
    out('| 구간 | 시간 | 층 |')
    out('|---|---|---|')
    flow_paths = []
    total = 0.0
    for (an, a), (bn, b) in zip(flow, flow[1:]):
        dist = dijkstra(t, [a])
        tt = time_to_reach(dist, b[0], b[1], 2)
        # 그림용 경로: 목적지 둘레에서 가장 가까운 칸으로
        best = min(((dist[y][x], x, y) for y in range(b[1] - 2, b[1] + 3) for x in range(b[0] - 2, b[0] + 3)
                    if 0 <= x < W and 0 <= y < H))
        p = path_back(t, dist, best[1], best[2])
        flow_paths.append(p)
        lv = []
        for (x, y) in p:
            v = t['level'][y][x]
            if not lv or lv[-1] != v:
                lv.append(v)
        total += tt
        out(f'| {an} → {bn} | {fmt_s(tt)} | {" → ".join(str(v) for v in lv)} |')
    out(f'| 합계 | {total:.1f}초 | |')

    # ── 랜드마크 시선 ──
    sl = sightlines(t)
    out('')
    out('## 랜드마크 시선 — 걷는 칸 가운데 랜드마크의 위쪽 절반이 한 화면(1600×900)에 걸리는 칸')
    out('| 줌 | 하나 이상 보임 | ' + ' | '.join(n for n, *_ in LANDMARKS) + ' |')
    out('|---|---|' + '---|' * len(LANDMARKS))
    for zoom, (seen, tot, per) in sl.items():
        out(f'| {zoom} | {seen}/{tot} ({100 * seen / tot:.0f}%) | ' + ' | '.join(f'{100 * v / tot:.0f}%' for v in per) + ' |')

    print('\n'.join(lines))
    draw_map(t, flow_paths, flow)
    draw_section(t)


# ── 그림 ─────────────────────────────────────────────────────────────────────
KIND_KO = {
    'Store': '잡화점', 'WeaponShop': '대장간', 'Temple': '예배당', 'Inn': '여관', 'House': '집', 'Armorer': '방어구점',
    'Tavern': '선술집', 'StarTower': '샛별 탑', 'AuctionHall': '경매장', 'GuildHall': '결사 회관', 'Warehouse': '창고',
    'Stable': '마구간', 'WaterMill': '물레방아간', 'WindMill': '풍차', 'GuardPost': '경비 초소', 'Scriptorium': '필경소',
    'Greenhouse': '약초 온실', 'Workshop': '공방', 'Lookout': '전망대',
}


def font(size):
    from PIL import ImageFont
    try:
        return ImageFont.truetype(os.path.join(ROOT, 'Client2D', 'assets', 'fonts', 'NanumSquareRoundB.ttf'), size)
    except OSError:
        return ImageFont.load_default()


def draw_map(t, flow_paths, flow):
    from PIL import Image, ImageDraw
    W, H = t['W'], t['H']
    S = 12
    PANEL = 300
    im = Image.new('RGB', (W * S + PANEL, H * S), (24, 26, 30))
    d = ImageDraw.Draw(im, 'RGBA')
    lvcol = [(84, 118, 70), (104, 138, 82), (126, 152, 92), (150, 166, 104), (176, 182, 120)]
    special = {'=': (158, 146, 128), ',': (188, 180, 162), ':': (146, 118, 86), 's': (198, 178, 132), '~': (64, 116, 176),
               'b': (146, 108, 72), 'g': (92, 118, 78), '#': (84, 82, 80), 'O': (70, 68, 66), 'T': (46, 86, 50),
               'X': (112, 108, 100), 'h': (58, 92, 54), 'c': (170, 158, 84), 'W': (128, 118, 106), 'L': (232, 204, 120),
               '*': (196, 112, 148), 'S': (206, 152, 92), 'F': (96, 156, 212), 'R': (60, 56, 52), 'G': (130, 118, 100)}
    for y in range(H):
        for x in range(W):
            c = t['ground'][y][x]
            lv = t['level'][y][x]
            col = special.get(c, lvcol[lv])
            if c in ('.',):
                col = lvcol[lv]
            elif c not in ('#', 'O', 'R', '~'):
                k = 0.92 + 0.06 * lv
                col = tuple(min(255, int(v * k)) for v in col)
            if c in STAIRS:
                col = (214, 206, 186)
            d.rectangle((x * S, y * S, x * S + S - 1, y * S + S - 1), fill=col)
            if c in STAIRS:
                dx, dy = STAIRS[c]
                cx, cy = x * S + S // 2, y * S + S // 2
                d.line((cx - dx * 4, cy - dy * 4, cx + dx * 4, cy + dy * 4), fill=(70, 56, 40), width=2)
    # 층 경계(절벽 · 옹벽) 굵게
    for y in range(H):
        for x in range(W):
            for dx, dy in ((1, 0), (0, 1)):
                nx, ny = x + dx, y + dy
                if nx < W and ny < H and t['level'][ny][nx] != t['level'][y][x]:
                    if dx:
                        d.line(((x + 1) * S, y * S, (x + 1) * S, y * S + S), fill=(16, 16, 18), width=2)
                    else:
                        d.line((x * S, (y + 1) * S, x * S + S, (y + 1) * S), fill=(16, 16, 18), width=2)
    # 건물
    f10 = font(10)
    for b in t['buildings']:
        box = (b['x0'] * S, b['y0'] * S, (b['x1'] + 1) * S - 1, (b['y1'] + 1) * S - 1)
        d.rectangle(box, fill=(150, 84, 64), outline=(250, 232, 204))
        label = KIND_KO.get(b['kind'], b['kind'])
        if b['kind'] != 'House':
            cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
            d.text((cx + 1, cy + 1), label, font=f10, fill=(0, 0, 0), anchor='mm')
            d.text((cx, cy), label, font=f10, fill=(255, 244, 222), anchor='mm')
        # 문
        dxs = {'S': ((b['x0'] + b['x1']) / 2, b['y1'] + 1), 'N': ((b['x0'] + b['x1']) / 2, b['y0']),
               'E': (b['x1'] + 1, (b['y0'] + b['y1']) / 2), 'W': (b['x0'], (b['y0'] + b['y1']) / 2)}[b['door']]
        d.ellipse((dxs[0] * S + S / 2 - 3, dxs[1] * S - 3, dxs[0] * S + S / 2 + 3, dxs[1] * S + 3), fill=(255, 220, 120))
    # 동선
    for i, p in enumerate(flow_paths):
        pts = [(x * S + S / 2, y * S + S / 2) for (x, y) in p]
        if len(pts) > 1:
            d.line(pts, fill=(255, 214, 90, 230), width=4, joint='curve')
    f12 = font(12)
    for i, (name, (x, y)) in enumerate(flow):
        cx, cy = x * S + S / 2, y * S + S / 2
        d.ellipse((cx - 9, cy - 9, cx + 9, cy + 9), fill=(40, 34, 28), outline=(255, 214, 90), width=2)
        d.text((cx, cy), str(i + 1), font=f12, fill=(255, 230, 160), anchor='mm')
    # 랜드마크
    for name, x, y, h in LANDMARKS:
        cx, cy = x * S + S / 2, y * S + S / 2
        star = [(cx + 11 * math.cos(math.radians(-90 + k * 36)) * (1 if k % 2 == 0 else 0.45),
                 cy + 11 * math.sin(math.radians(-90 + k * 36)) * (1 if k % 2 == 0 else 0.45)) for k in range(10)]
        d.polygon(star, fill=(120, 214, 226), outline=(20, 40, 50))
    # 구획 이름
    f14 = font(14)
    for name, sub, x, y in t['districts'][1:]:
        cx, cy = x * S, y * S
        d.text((cx + 1, cy + 1), name, font=f14, fill=(0, 0, 0), anchor='mm')
        d.text((cx, cy), name, font=f14, fill=(255, 246, 220), anchor='mm')
    # 오른쪽 범례
    ox = W * S + 16
    d.text((ox, 16), '데니차 설계도', font=font(18), fill=(236, 230, 216))
    yy = 50
    d.text((ox, yy), '층(바닥 밝기)', font=f12, fill=(200, 196, 186)); yy += 20
    names = ['0 강가 · 시장 · 남문', '1 광장 · 장인 · 여관', '2 결사 회관 단 · 들', '3 윗마을 · 예배당', '4 풍차 · 전망대']
    for i, n in enumerate(names):
        d.rectangle((ox, yy, ox + 16, yy + 14), fill=lvcol[i], outline=(20, 20, 20))
        d.text((ox + 24, yy), n, font=f12, fill=(222, 218, 208)); yy += 20
    yy += 8
    d.line((ox, yy + 7, ox + 16, yy + 7), fill=(16, 16, 18), width=3)
    d.text((ox + 24, yy), '층 경계(옹벽 · 절벽)', font=f12, fill=(222, 218, 208)); yy += 20
    d.rectangle((ox, yy, ox + 16, yy + 14), fill=(214, 206, 186))
    d.text((ox + 24, yy), '계단(화살표 = 오르는 쪽)', font=f12, fill=(222, 218, 208)); yy += 20
    d.ellipse((ox + 4, yy + 3, ox + 12, yy + 11), fill=(255, 220, 120))
    d.text((ox + 24, yy), '건물 문', font=f12, fill=(222, 218, 208)); yy += 20
    d.polygon([(ox + 8, yy), (ox + 11, yy + 6), (ox + 16, yy + 7), (ox + 12, yy + 11), (ox + 13, yy + 16),
               (ox + 8, yy + 13), (ox + 3, yy + 16), (ox + 4, yy + 11), (ox, yy + 7), (ox + 5, yy + 6)], fill=(120, 214, 226))
    d.text((ox + 24, yy), '랜드마크(탑 · 첨탑 · 풍차 …)', font=f12, fill=(222, 218, 208)); yy += 26
    d.line((ox, yy + 7, ox + 16, yy + 7), fill=(255, 214, 90), width=4)
    d.text((ox + 24, yy), '처음 온 사람의 동선', font=f12, fill=(222, 218, 208)); yy += 22
    for i, (name, _) in enumerate(flow):
        d.text((ox + 8, yy), f'{i + 1}. {name}', font=f12, fill=(236, 222, 180)); yy += 18
    os.makedirs(IMG, exist_ok=True)
    im.save(os.path.join(IMG, 'level_denica_map.png'), optimize=True)


def draw_section(t):
    """남문 → 광장 → 결사 회관 단 → 윗마을 → 예배당: 걸어가는 길을 따라 편 높이 단면."""
    from PIL import Image, ImageDraw
    W, H = t['W'], t['H']
    homes = t['homes']
    stops = [('남문', (37, 70)), ('분수 광장', (41, 53)), ('결사 회관 단', (40, 27)), ('윗마을', (62, 25)), ('예배당', (61, 10))]
    pts = []
    for (an, a), (bn, b) in zip(stops, stops[1:]):
        dist = dijkstra(t, [a])
        p = path_back(t, dist, b[0], b[1])
        if pts:
            p = p[1:]
        pts += p
    prof = [t['level'][y][x] + (0.5 if t['ground'][y][x] in STAIRS else 0.0) for (x, y) in pts]
    # 계단 칸은 반 층(오르는 중)
    Wd, Hd = 1100, 300
    im = Image.new('RGB', (Wd, Hd), (26, 28, 32))
    d = ImageDraw.Draw(im, 'RGBA')
    n = len(prof)
    x0, x1, yb = 50, Wd - 20, Hd - 60
    sy = 40
    for i in range(n - 1):
        xa = x0 + (x1 - x0) * i / (n - 1)
        xb = x0 + (x1 - x0) * (i + 1) / (n - 1)
        ya, yb2 = yb - prof[i] * sy, yb - prof[i + 1] * sy
        d.polygon([(xa, yb), (xa, ya), (xb, yb2), (xb, yb)], fill=(96, 120, 84))
        d.line((xa, ya, xb, yb2), fill=(214, 206, 186), width=3)
    for lv in range(5):
        y = yb - lv * sy
        d.line((x0, y, x1, y), fill=(255, 255, 255, 30), width=1)
        d.text((12, y), f'{lv}층', font=font(12), fill=(170, 166, 156), anchor='lm')
    # 구간 표시
    acc = 0
    idx = [0]
    for (an, a), (bn, b) in zip(stops, stops[1:]):
        dist = dijkstra(t, [a])
        p = path_back(t, dist, b[0], b[1])
        acc += len(p) - 1
        idx.append(acc)
    f = font(13)
    for (name, _), i in zip(stops, idx):
        x = x0 + (x1 - x0) * min(i, n - 1) / (n - 1)
        d.line((x, yb + 4, x, 30), fill=(255, 214, 90, 120), width=1)
        d.text((x, yb + 22), name, font=f, fill=(255, 236, 190), anchor='mm')
    d.text((x0, 14), '높이 단면 — 남문에서 예배당까지 걸어가는 길(가로 = 걸음 수, 세로 = 층)', font=f, fill=(230, 226, 214))
    d.text((x1, Hd - 16), f'{n - 1}걸음', font=font(12), fill=(170, 166, 156), anchor='rm')
    im.save(os.path.join(IMG, 'level_denica_section.png'), optimize=True)


if __name__ == '__main__':
    main()
