#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 마을 생성기(Phase 4-A) — tools/mapgen/<마을>/의 텍스트 층을 읽어 검사한 뒤
#   · GameServer/Data/TownMap.h   서버 · 클라 공유 헤더(생성물 — 직접 고치지 않는다)
#   · <마을>/preview.png          위에서 본 미리보기(층 음영 · 건물 · 구획 이름)
# 를 만든다.
#
#   python3 tools/mapgen/gen_town.py [tools/mapgen/denica]
#
# 원본(사람이 고친다):
#   town.txt       이름 · Zone · 원점 · 크기
#   ground.txt     칸마다 바닥 · 막힘 문자(아래 LEGEND). 행 = y
#   height.txt     칸마다 층 0~4(계단 칸은 낮은 쪽 층)
#   buildings.txt  건물 사각형(종류 · 문 · 변형 · 간판 — 간판의 빈칸은 _)
#   props.txt      꾸밈 소품(종류 · 자리 · 값) — 막히는 소품은 ground.txt의 문자가 막는다
#   anchors.txt    이름 있는 자리(부활 · 허수아비 · 훈련장 · 남문 밖 …)
#   districts.txt  구획 이름 · 설명 · 투어 자리
#   ambient.txt    소리 구역(종류 · 자리 · 반경)
#
# 높이 규칙(서버의 불리언 충돌 격자로 절벽을 표현한다 — PHASE_4.md 1장):
#   · 상하좌우로 맞닿은 걷는 두 칸은 층이 같거나, 한쪽이 계단이고 그 계단이 두 칸을 잇는다.
#   · 낮은 걷는 칸과 맞닿은 높은 걷는 칸은 생성기가 가장자리(R — 막힘)로 바꾼다.
#   · 계단(^ v < > — 그 방향으로 한 층 오른다)의 옆 칸은 막혔거나 같은 방향 · 같은 층 계단이어야 한다.
# ─────────────────────────────────────────────────────────────────────────────
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

# 문자 → (걸을 수 있는가, 설명)
LEGEND = {
    '.': (True, '풀밭'), '=': (True, '포석 길'), ',': (True, '광장 판석'), ':': (True, '흙길'),
    's': (True, '모래(훈련장)'), 'G': (True, '성문'), 'b': (True, '다리'), 'g': (True, '묘지 풀'),
    'N': (True, '(옛 NPC 자리 — 광장 판석)'),
    '^': (True, '계단 ↑(북으로 오른다)'), 'v': (True, '계단 ↓(남으로 오른다)'),
    '<': (True, '계단 ←(서로 오른다)'), '>': (True, '계단 →(동으로 오른다)'),
    '#': (False, '성벽'), 'O': (False, '망루'), 'B': (False, '건물'), 'F': (False, '분수'),
    'T': (False, '나무'), 'L': (False, '가로등'), '*': (False, '꽃밭'), 'S': (False, '가판대'),
    'R': (False, '가장자리(절벽 위 난간 — 생성기가 만든다)'), '~': (False, '물'), 'X': (False, '바위'),
    'h': (False, '산울타리'), 'c': (False, '밭(작물)'), 'P': (False, '막히는 소품(props.txt)'),
    'W': (False, '낮은 담'),
}
STAIRS = {'^': (0, -1), 'v': (0, 1), '<': (-1, 0), '>': (1, 0)}

# buildings.txt의 이름 → TownTypes.h BuildingKind 열거자(e 뒤)
BUILDING_KINDS = {
    # Phase 4 전부터
    'store': 'Store', 'weaponshop': 'WeaponShop', 'temple': 'Temple', 'inn': 'Inn', 'house': 'House',
    # Phase 4-B · 4-C(열거형 끝에만 더한다 — TownTypes.h)
    'armorer': 'Armorer', 'tavern': 'Tavern', 'tower': 'StarTower', 'auction': 'AuctionHall', 'guildhall': 'GuildHall',
    'warehouse': 'Warehouse', 'stable': 'Stable', 'watermill': 'WaterMill', 'windmill': 'WindMill', 'guardpost': 'GuardPost',
    'scriptorium': 'Scriptorium', 'greenhouse': 'Greenhouse', 'workshop': 'Workshop', 'lookout': 'Lookout',
}
PROP_KINDS = []
AMBIENT_KINDS = ['fountain', 'market', 'smithy', 'bell', 'river', 'wind']
# 소품(props.txt) — 막히는 소품: ground.txt의 'P' 칸 하나에 하나. 줄 소품: 두 끝(x y x2 y2) 사이에 매단다(막지 않는다)
PROP_BLOCK = {'grave': 'Grave', 'board': 'Board', 'bench': 'Bench', 'statue': 'Statue', 'flagpole': 'Flagpole', 'well': 'Well',
              'cart': 'Cart', 'hay': 'Hay', 'barrels': 'Barrels', 'crates': 'Crates', 'signpost': 'Signpost', 'target': 'Target',
              'rack': 'Rack'}
PROP_LINE = {'laundry': 'Laundry', 'bunting': 'Bunting'}


def die(msg):
    print('[gen_town] 오류: ' + msg)
    sys.exit(1)


def read_lines(path):
    out = []
    if not os.path.exists(path):
        return out
    for raw in open(path, encoding='utf-8'):
        line = raw.split('#', 1)[0].rstrip() if not raw.lstrip().startswith('#') else ''
        if line.strip():
            out.append(line)
    return out


def read_grid(path, w, h, name):
    rows = [l.rstrip('\n') for l in open(path, encoding='utf-8') if l.strip() != '']
    if len(rows) != h:
        die('%s: 줄 수 %d ≠ 높이 %d' % (name, len(rows), h))
    for y, r in enumerate(rows):
        if len(r) != w:
            die('%s %d번째 줄: 길이 %d ≠ 너비 %d' % (name, y, len(r), w))
    return [list(r) for r in rows]


def main():
    town_dir = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'tools', 'mapgen', 'denica'))
    cfg = {}
    for l in read_lines(os.path.join(town_dir, 'town.txt')):
        k, v = l.split(None, 1)
        cfg[k] = v.strip()
    x0, y0 = (int(v) for v in cfg['origin'].split())
    W, H = (int(v) for v in cfg['size'].split())
    ground = read_grid(os.path.join(town_dir, 'ground.txt'), W, H, 'ground.txt')
    level_ch = read_grid(os.path.join(town_dir, 'height.txt'), W, H, 'height.txt')
    level = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            c = ground[y][x]
            if c not in LEGEND:
                die('ground.txt (%d, %d): 모르는 문자 %r' % (x, y, c))
            d = level_ch[y][x]
            if not d.isdigit() or int(d) > 4:
                die('height.txt (%d, %d): 층은 0~4 — %r' % (x, y, d))
            level[y][x] = int(d)

    def inside(x, y):
        return 0 <= x < W and 0 <= y < H

    def walk(x, y):
        return inside(x, y) and LEGEND[ground[y][x]][0]

    # 가장자리로 바꾸기 전의 바닥 재질(가장자리 윗면도 원래 재질로 그린다 — kSurface)
    surface = [row[:] for row in ground]

    # ── 가장자리 자동 막기(되풀이 — 새로 막힌 칸이 다른 칸의 판단을 바꾸지 않을 때까지) ──
    rims = 0
    errors = []
    changed = True
    while changed:
        changed = False
        for y in range(H):
            for x in range(W):
                c = ground[y][x]
                if not walk(x, y) or c in STAIRS:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if not walk(nx, ny):
                        continue
                    n = ground[ny][nx]
                    if n in STAIRS:
                        # 계단이 이 칸을 잇는가: 계단의 오르는 쪽(윗칸) = 층 + 1, 반대쪽(아랫칸) = 층
                        sdx, sdy = STAIRS[n]
                        if (x, y) == (nx + sdx, ny + sdy):
                            ok = level[y][x] == level[ny][nx] + 1
                        elif (x, y) == (nx - sdx, ny - sdy):
                            ok = level[y][x] == level[ny][nx]
                        else:
                            ok = False   # 계단 옆 — 아래에서 따로 검사
                        if not ok and level[y][x] > level[ny][nx]:
                            ground[y][x] = 'R'; rims += 1; changed = True
                            break
                        continue
                    if level[y][x] > level[ny][nx]:
                        ground[y][x] = 'R'; rims += 1; changed = True
                        break

    # ── 계단 검사 ──
    for y in range(H):
        for x in range(W):
            c = ground[y][x]
            if c not in STAIRS:
                continue
            dx, dy = STAIRS[c]
            L = level[y][x]
            if L >= 4:
                errors.append('계단 (%d, %d): 4층에서는 더 오를 수 없다' % (x, y))
            up, down = (x + dx, y + dy), (x - dx, y - dy)
            if not walk(*up) or (ground[up[1]][up[0]] not in STAIRS and level[up[1]][up[0]] != L + 1):
                errors.append('계단 (%d, %d): 오르는 쪽 (%d, %d)은 %d층의 걷는 칸이어야 한다' % (x, y, up[0], up[1], L + 1))
            if not walk(*down) or (ground[down[1]][down[0]] not in STAIRS and level[down[1]][down[0]] != L):
                errors.append('계단 (%d, %d): 내려오는 쪽 (%d, %d)은 %d층의 걷는 칸이어야 한다' % (x, y, down[0], down[1], L))
            for sx, sy in ((dy, dx), (-dy, -dx)):   # 옆
                ax, ay = x + sx, y + sy
                if not walk(ax, ay):
                    continue
                if ground[ay][ax] == c and level[ay][ax] == L:
                    continue
                errors.append('계단 (%d, %d): 옆 칸 (%d, %d)은 막혔거나 같은 방향 · 같은 층 계단이어야 한다' % (x, y, ax, ay))

    # ── 최종 규칙 검사: 맞닿은 걷는 두 칸 ──
    def link_ok(ax, ay, bx, by):
        a, b = ground[ay][ax], ground[by][bx]
        if a not in STAIRS and b not in STAIRS:
            return level[ay][ax] == level[by][bx]
        if b in STAIRS and a not in STAIRS:
            return link_ok(bx, by, ax, ay)
        sdx, sdy = STAIRS[a]
        if (bx, by) == (ax + sdx, ay + sdy):
            return (level[by][bx] == level[ay][ax] + 1) or (b == a and level[by][bx] == level[ay][ax] + 1)
        if (bx, by) == (ax - sdx, ay - sdy):
            return level[by][bx] == level[ay][ax] or (b == a and level[by][bx] == level[ay][ax] - 1)
        return b == a and level[by][bx] == level[ay][ax]
    for y in range(H):
        for x in range(W):
            if not walk(x, y):
                continue
            for dx, dy in ((1, 0), (0, 1)):
                nx, ny = x + dx, y + dy
                if walk(nx, ny) and not link_ok(x, y, nx, ny):
                    errors.append('(%d, %d) ↔ (%d, %d): 층 %d · %d가 계단 없이 맞닿았다' % (x, y, nx, ny, level[y][x], level[ny][nx]))

    # ── 건물 ──
    buildings = []
    owner = [[-1] * W for _ in range(H)]
    for l in read_lines(os.path.join(town_dir, 'buildings.txt')):
        parts = l.split()
        kind = parts[0]
        if kind not in BUILDING_KINDS:
            die('buildings.txt: 모르는 건물 종류 %r' % kind)
        bx0, by0, bx1, by1 = (int(v) for v in parts[1:5])
        opts = dict(p.split('=', 1) for p in parts[5:])
        if not (0 <= bx0 <= bx1 < W and 0 <= by0 <= by1 < H):
            die('buildings.txt: %s (%d %d %d %d)이 마을 밖' % (kind, bx0, by0, bx1, by1))
        idx = len(buildings)
        for y in range(by0, by1 + 1):
            for x in range(bx0, bx1 + 1):
                if ground[y][x] != 'B':
                    errors.append('건물 %s (%d, %d): 발자국 칸이 B가 아니다(%r)' % (kind, x, y, ground[y][x]))
                if owner[y][x] >= 0:
                    errors.append('건물 %s (%d, %d): 다른 건물과 겹친다' % (kind, x, y))
                owner[y][x] = idx
        lv = {level[y][x] for y in range(by0, by1 + 1) for x in range(bx0, bx1 + 1)}
        if len(lv) != 1:
            errors.append('건물 %s (%d %d): 발자국이 한 층 위에 있어야 한다(%s)' % (kind, bx0, by0, sorted(lv)))
        buildings.append(dict(kind=kind, x0=bx0, y0=by0, x1=bx1, y1=by1, door=opts.get('door', 'S')[0],
                              variant=int(opts.get('variant', '0')), sign=opts.get('sign', '').replace('_', ' '),
                              level=min(lv)))
    for y in range(H):
        for x in range(W):
            if ground[y][x] == 'B' and owner[y][x] < 0:
                errors.append('(%d, %d): B 칸이 어느 건물에도 들지 않는다' % (x, y))

    # ── 이름 있는 자리 ──
    spawns, dummies, named, yard, npcs = [], [], [], None, []
    for l in read_lines(os.path.join(town_dir, 'anchors.txt')):
        parts = l.split()
        key = parts[0]
        if key == 'yard':
            yard = tuple(int(v) for v in parts[1:5])
            continue
        if key == 'npc':
            nid, nx, ny = (int(v) for v in parts[1:4])
            if not walk(nx, ny):
                errors.append('npc %d (%d, %d): 걸을 수 있는 칸이어야 한다' % (nid, nx, ny))
            if any(n[0] == nid for n in npcs):
                errors.append('npc %d: 두 번 적었다' % nid)
            npcs.append((nid, nx, ny))
            continue
        fx, fy = float(parts[1]), float(parts[2])
        ix, iy = int(round(fx)), int(round(fy))
        if key in ('spawn', 'dummy'):
            if not walk(ix, iy):
                errors.append('%s (%d, %d): 걸을 수 있는 칸이어야 한다' % (key, ix, iy))
            (spawns if key == 'spawn' else dummies).append((ix, iy))
        else:
            named.append((key, fx, fy))
    if yard is None:
        die('anchors.txt: yard(훈련장 사각형)가 없다')

    districts = []
    for l in read_lines(os.path.join(town_dir, 'districts.txt')):
        f = [p.strip() for p in l.split('|')]
        tx, ty = (float(v) for v in f[2].split())
        districts.append((f[0], f[1], tx, ty, float(f[3])))

    ambient = []
    for l in read_lines(os.path.join(town_dir, 'ambient.txt')):
        parts = l.split()
        if parts[0] not in AMBIENT_KINDS:
            die('ambient.txt: 모르는 소리 %r' % parts[0])
        ambient.append((parts[0], float(parts[1]), float(parts[2]), float(parts[3])))

    props = []
    pown = {}
    props_path = os.path.join(town_dir, 'props.txt')
    for l in (read_lines(props_path) if os.path.exists(props_path) else []):
        parts = l.split()
        kind = parts[0]
        if kind in PROP_BLOCK:
            px, py = int(parts[1]), int(parts[2])
            if not inside(px, py) or ground[py][px] != 'P':
                errors.append('소품 %s (%d, %d): ground.txt의 P 칸이어야 한다(%r)' % (kind, px, py, ground[py][px] if inside(px, py) else '밖'))
            if (px, py) in pown:
                errors.append('소품 %s (%d, %d): 한 칸에 둘' % (kind, px, py))
            pown[(px, py)] = kind
            props.append((kind, px, py, px, py))
        elif kind in PROP_LINE:
            ax, ay, bx, by = (int(v) for v in parts[1:5])
            if not (inside(ax, ay) and inside(bx, by)):
                errors.append('줄 소품 %s (%d %d %d %d): 마을 밖' % (kind, ax, ay, bx, by))
            if level[ay][ax] != level[by][bx]:
                errors.append('줄 소품 %s (%d %d %d %d): 두 끝의 층이 같아야 한다' % (kind, ax, ay, bx, by))
            props.append((kind, ax, ay, bx, by))
        else:
            die('props.txt: 모르는 소품 %r' % kind)
    for y in range(H):
        for x in range(W):
            if ground[y][x] == 'P' and (x, y) not in pown:
                errors.append('(%d, %d): P 칸에 소품이 없다(props.txt)' % (x, y))

    # ── 이어짐: 첫 부활 자리에서 걸어서 닿지 못하는 걷는 칸(서버 대각 규칙 — 두 옆이 다 막히면 못 간다) ──
    from collections import deque
    unreach = []
    if spawns:
        seen = {spawns[0]}
        q = deque([spawns[0]])
        while q:
            cx, cy = q.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
                nx, ny = cx + dx, cy + dy
                if (nx, ny) in seen or not walk(nx, ny):
                    continue
                if dx and dy and not walk(cx + dx, cy) and not walk(cx, cy + dy):
                    continue
                seen.add((nx, ny))
                q.append((nx, ny))
        unreach = [(x, y) for y in range(H) for x in range(W) if walk(x, y) and (x, y) not in seen and 0 < x < W - 1 and 0 < y < H - 1]
        for key, fx, fy in named:
            ix, iy = int(round(fx)), int(round(fy))
            if walk(ix, iy) and (ix, iy) not in seen and 0 < ix < W - 1 and 0 < iy < H - 1:
                errors.append('%s (%d, %d): 부활 자리에서 걸어서 닿지 않는다' % (key, ix, iy))
        for x, y in dummies:
            if (x, y) not in seen:
                errors.append('dummy (%d, %d): 부활 자리에서 걸어서 닿지 않는다' % (x, y))
        for nid, x, y in npcs:
            if (x, y) not in seen:
                errors.append('npc %d (%d, %d): 부활 자리에서 걸어서 닿지 않는다' % (nid, x, y))
    if unreach:
        print('[gen_town] 경고: 걸어서 닿지 않는 걷는 칸 %d개(마을 안쪽) — 예: %s' % (len(unreach), ' '.join('(%d,%d)' % p for p in unreach[:12])))

    if errors:
        for e in errors[:60]:
            print('[gen_town] ' + e)
        die('%d건 — 위를 고친 뒤 다시' % len(errors))

    # ── 쓰기: TownMap.h ──
    def lit(s):
        return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'
    out = []
    out.append('#pragma once')
    out.append('// ─────────────────────────────────────────────────────────────────────────────')
    out.append('// 생성물 — tools/mapgen/gen_town.py가 tools/mapgen/%s/*.txt에서 만든다. 직접 고치지 말 것.' % os.path.basename(town_dir))
    out.append('// 서버(충돌 · NPC · 부활)와 클라(지형 · 건물 · 미니맵)가 같이 쓴다.')
    out.append('// ─────────────────────────────────────────────────────────────────────────────')
    out.append('#include "TownTypes.h"')
    out.append('')
    out.append('namespace TownMap')
    out.append('{')
    out.append('\tinline constexpr const char* kName = %s;' % lit(cfg['name']))
    out.append('\tinline constexpr int kZoneId = %d;' % int(cfg['zone']))
    out.append('\tinline constexpr int kX0 = %d, kY0 = %d, kW = %d, kH = %d;' % (x0, y0, W, H))
    out.append('')
    out.append('\t// 바닥 · 막힘 문자(행 = y) — 가장자리(R)는 생성기가 채웠다')
    out.append('\tinline constexpr const char* kGround[kH] =')
    out.append('\t{')
    for r in ground:
        out.append('\t\t%s,' % lit(''.join(r)))
    out.append('\t};')
    out.append('\t// 바닥 재질 문자(가장자리 R로 바꾸기 전 — 가장자리 윗면도 이 재질로 그린다)')
    out.append('\tinline constexpr const char* kSurface[kH] =')
    out.append('\t{')
    for r in surface:
        out.append('\t\t%s,' % lit(''.join(r)))
    out.append('\t};')
    out.append('\t// 층 0~4(계단 칸은 낮은 쪽 층)')
    out.append('\tinline constexpr const char* kLevel[kH] =')
    out.append('\t{')
    for r in level:
        out.append('\t\t%s,' % lit(''.join(str(v) for v in r)))
    out.append('\t};')
    out.append('')
    out.append('\tinline constexpr BuildingRow kBuildings[] =')
    out.append('\t{')
    for b in buildings:
        out.append('\t\t{ BuildingKind::e%s, %d, %d, %d, %d, %d, \'%s\', %d, %s },' % (
            BUILDING_KINDS[b['kind']],
            x0 + b['x0'], y0 + b['y0'], x0 + b['x1'], y0 + b['y1'], b['level'], b['door'], b['variant'], lit(b['sign'])))
    out.append('\t};')
    out.append('')
    out.append('\tinline constexpr Point16 kSpawns[] = { %s };' % ', '.join('{ %d, %d }' % (x0 + x, y0 + y) for x, y in spawns))
    out.append('\tinline constexpr Point16 kDummies[] = { %s };' % ', '.join('{ %d, %d }' % (x0 + x, y0 + y) for x, y in dummies))
    out.append('\tinline constexpr int kYardX0 = %d, kYardY0 = %d, kYardX1 = %d, kYardY1 = %d;   // 양끝 포함' % (
        x0 + yard[0], y0 + yard[1], x0 + yard[2], y0 + yard[3]))
    out.append('')
    out.append('\t// 이름 있는 자리(월드 칸 — 소수는 칸 사이)')
    for key, fx, fy in named:
        out.append('\tinline constexpr PointF k%s{ %.2ff, %.2ff };' % (''.join(p.capitalize() for p in key.split('_')), x0 + fx, y0 + fy))
    out.append('')
    out.append('\t// 마을 NPC의 집(배회 중심, 월드 칸) — 이름 · 말은 WorldData::kNpcs, 자리는 여기(anchors.txt의 npc)')
    out.append('\tinline constexpr NpcHome kNpcHomes[] =')
    out.append('\t{')
    for nid, x, y in sorted(npcs):
        out.append('\t\t{ %d, %d, %d },' % (nid, x0 + x, y0 + y))
    if not npcs:
        out.append('\t\t{ 0, 0, 0 },   // (없음 — 자리 채우기)')
    out.append('\t};')
    out.append('')
    out.append('\tinline constexpr DistrictRow kDistricts[] =')
    out.append('\t{')
    for name, sub, tx, ty, z in districts:
        out.append('\t\t{ %s, %s, %.2ff, %.2ff, %.2ff },' % (lit(name), lit(sub), x0 + tx, y0 + ty, z))
    out.append('\t};')
    out.append('\tinline constexpr AmbientRow kAmbient[] =')
    out.append('\t{')
    for kind, ax, ay, r in ambient:
        out.append('\t\t{ AmbientKind::e%s, %.2ff, %.2ff, %.2ff },' % (kind.capitalize(), x0 + ax, y0 + ay, r))
    if not ambient:
        out.append('\t\t{ AmbientKind::eWind, 0.f, 0.f, 0.f },   // (없음 — 자리 채우기)')
    out.append('\t};')
    out.append('')
    out.append('\t// 소품(props.txt) — 막히는 소품은 kGround의 P 칸(서버 충돌), 줄 소품(빨랫줄 · 줄깃발)은 두 끝 사이(막지 않는다)')
    out.append('\tinline constexpr PropRow kProps[] =')
    out.append('\t{')
    for kind, ax, ay, bx, by in props:
        out.append('\t\t{ PropKind::e%s, %d, %d, %d, %d },' % (PROP_BLOCK.get(kind) or PROP_LINE[kind], x0 + ax, y0 + ay, x0 + bx, y0 + by))
    if not props:
        out.append('\t\t{ PropKind::eBench, -1, -1, -1, -1 },   // (없음 — 자리 채우기, x < 0은 건너뛴다)')
    out.append('\t};')
    out.append('}')
    header = '\n'.join(out) + '\n'
    path = os.path.join(ROOT, 'GameServer', 'Data', 'TownMap.h')
    old = open(path, encoding='utf-8').read() if os.path.exists(path) else ''
    if old != header:
        open(path, 'w', encoding='utf-8', newline='\n').write(header)

    # ── 미리보기 PNG ──
    try:
        write_preview(town_dir, ground, level, buildings, districts, W, H)
    except ImportError:
        print('[gen_town] (PIL 없음 — 미리보기 생략)')

    walkable = sum(1 for y in range(H) for x in range(W) if walk(x, y))
    hist = [sum(1 for y in range(H) for x in range(W) if level[y][x] == k and walk(x, y)) for k in range(5)]
    print('[gen_town] %s %dx%d — 걷기 %d · 막힘 %d · 가장자리 %d · 건물 %d · 소품 %d · 층별 걷기 %s' % (
        cfg['name'], W, H, walkable, W * H - walkable, rims, len(buildings), len(props), hist))


def write_preview(town_dir, ground, level, buildings, districts, W, H):
    from PIL import Image, ImageDraw, ImageFont
    S = 10
    im = Image.new('RGB', (W * S, H * S), (20, 22, 26))
    d = ImageDraw.Draw(im)
    base = {
        '.': (96, 128, 72), '=': (150, 140, 124), ',': (182, 176, 160), ':': (140, 112, 80), 's': (196, 176, 130),
        'G': (120, 110, 96), 'b': (140, 104, 70), 'g': (84, 110, 70), 'N': (182, 176, 160),
        '#': (90, 88, 86), 'O': (70, 68, 66), 'B': (150, 80, 62), 'F': (90, 150, 210), 'T': (46, 90, 50),
        'L': (230, 200, 120), '*': (200, 110, 150), 'S': (200, 150, 90), 'R': (110, 104, 96), '~': (60, 110, 170),
        'X': (110, 106, 100), 'h': (60, 96, 56), 'c': (170, 160, 80), 'P': (130, 110, 90), 'W': (130, 120, 108),
    }
    for y in range(H):
        for x in range(W):
            c = ground[y][x]
            col = base.get(c, (255, 0, 255))
            if c in STAIRS:
                col = (200, 196, 180)
            k = 1.0 + 0.09 * level[y][x]
            col = tuple(min(255, int(v * k)) for v in col)
            d.rectangle((x * S, y * S, x * S + S - 1, y * S + S - 1), fill=col)
            if c in STAIRS:
                dx, dy = STAIRS[c]
                cx, cy = x * S + S // 2, y * S + S // 2
                d.line((cx - dx * 3, cy - dy * 3, cx + dx * 3, cy + dy * 3), fill=(60, 50, 40), width=2)
            # 층 경계선
            for dx, dy in ((1, 0), (0, 1)):
                nx, ny = x + dx, y + dy
                if nx < W and ny < H and level[ny][nx] != level[y][x]:
                    if dx:
                        d.line(((x + 1) * S - 1, y * S, (x + 1) * S - 1, y * S + S - 1), fill=(20, 20, 24))
                    else:
                        d.line((x * S, (y + 1) * S - 1, x * S + S - 1, (y + 1) * S - 1), fill=(20, 20, 24))
    for b in buildings:
        d.rectangle((b['x0'] * S, b['y0'] * S, (b['x1'] + 1) * S - 1, (b['y1'] + 1) * S - 1), outline=(250, 230, 200))
    try:
        font = ImageFont.truetype(os.path.join(ROOT, 'Client2D', 'assets', 'fonts', 'NanumSquareRoundB.ttf'), 13)
    except OSError:
        font = None
    x0 = y0 = 0
    for name, sub, tx, ty, z in districts:
        px, py = tx * S, ty * S
        d.text((px + 1, py + 1), name, fill=(0, 0, 0), font=font, anchor='mm' if font else None)
        d.text((px, py), name, fill=(255, 240, 200), font=font, anchor='mm' if font else None)
    im.save(os.path.join(town_dir, 'preview.png'))


if __name__ == '__main__':
    main()
