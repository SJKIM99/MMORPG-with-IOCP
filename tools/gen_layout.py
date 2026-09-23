"""world/regions/*.json 생성기.

CLAUDE.md 5장: 월드 배치는 코드가 아니라 데이터다.
씬에 손으로 놓지 않고 이 스크립트가 JSON 을 만들며, 서버와 Godot 임포터가
그 JSON 하나를 함께 읽는다. 지형(높이맵)도 같은 파일 안에 들어간다.

seed 를 고정했으므로 같은 입력이면 항상 같은 월드가 나온다.
프롭 간 간격은 상수로 때려박지 않고 gltf_bounds 로 실측한 반경에서 계산한다.

사용법:
    python tools/gen_layout.py            # 전부 생성
    python tools/gen_layout.py town       # 마을만
"""

from __future__ import annotations

import json
import math
import random
import sys
from datetime import date
from pathlib import Path

from gltf_bounds import Bounds, asset_path, load_manifest, read_bounds
from terrain import BoxPlateau, Bump, Heightmap, Plateau, build_heightmap

SEED = 20260922

REPO = Path(__file__).resolve().parent.parent
OUT_DIR = REPO / "world" / "regions"

# CLAUDE.md 4장 — 협상 대상 아님
TOWN_SIZE = 256
FIELD_SIZE = 512

# 스폰 지점·NPC 주변은 반경 3m 를 비운다 (5장)
CLEAR_RADIUS = 3.0

# 계단 밑 지형을 이만큼 더 깎는다. 계단면과 지형이 같은 높이면 z-fighting 으로
# 디딤판이 잔디에 먹히고, 얕게 깎으면 계단이 언덕에 파묻혀 난간만 보인다.
STAIR_CUT = 0.30

# 걸어 오를 수 있는 최대 기울기(tan). Godot 의 FloorMaxAngle 52도(tan 1.28)보다
# 낮게 잡아야 한다. 넘어서면 그 면이 "벽"으로 분류되어 걸어 오를 수 없고,
# step-up 로직이 개입해 캐릭터를 한 번에 들어 올린다 — 순간이동의 원인이다.
# 40도(tan 0.84) 로 여유를 둔다.
MAX_WALK_SLOPE = 0.84

# 역할별 대기 동작. (기본 클립, 가끔 섞을 클립들)
#
# KayKit Character Animations FREE 티어에 실제로 들어 있는 것만 쓴다.
# 확인한 목록: Idle_A, Idle_B, Interact, PickUp, Use_Item, Throw,
#             Walking_A/B/C, Running_A/B, Jump_*, Hit_A/B, Death_A/B
# Sit / Wave / Talk 는 **없다.** 유료 티어에만 있으므로 쓰지 않는다.
#
# 클라이언트 연출이지만 코드가 아니라 데이터로 둔다(5장). 나중에 서버가
# 상호작용 결과로 동작을 지정하게 되면 같은 필드를 채우기만 하면 된다.
# 물길. 꺾은선의 (x, z) 만 적고 수면 높이는 지형에서 계산한다(_stream_profile).
#
# 마을 개울은 서쪽 x≈62 를 따라 북에서 남으로 흐른다. 그 축의 지형이 실제로
# 북 2.9m -> 남 -0.6m 로 내려가서 물이 자연스럽게 흐른다. 억지로 판 것이 아니다.
# 대로(z=128)와 한 번 교차하므로 그 자리에 다리를 놓는다.
TOWN_STREAM = [
    (66.0, 18.0),
    (62.0, 58.0),
    (68.0, 96.0),
    (62.0, 128.0),    # 대로 교차 — 다리
    (66.0, 148.0),
    (66.0, 150.0),    # 물레방아 둑 — 여기서 떨어진다
    (61.0, 190.0),
    (64.0, 252.0),
]

# 둑 위치(꺾은선 인덱스)와 낙차(m).
#
# 격자 한 칸(2m)에 3m 를 떨군다 = 56도. Godot 의 FloorMaxAngle(52도)을
# **일부러** 넘겨서 이 면이 바닥이 아니라 벽으로 분류되게 한다.
#
# 이게 안전한 이유: step-up 한계가 0.75m 라 3m 벽은 올라가지지 않는다.
# 위험한 것은 그 사이(0.2~0.75m)의 애매한 턱이고, 그건 step-up 이 성공해서
# 캐릭터를 순간이동시킨다. 넘을 수 없을 만큼 높으면 그냥 막힐 뿐이다.
# 떨어지는 것은 막히지 않으므로 하류로는 그대로 흘러내려간다.
#
# 필드(격자 8m)에서는 이 짓을 할 수 없어 급류로 간다.
TOWN_WEIR = (5, 3.0)

# 다리 상판의 **끝** 높이(모델 로컬, 배율 적용 전).
#
# glTF 정점을 직접 재서 얻었다. 통행로(|z| < 1.5) 단면의 최고 y 는
#     끝 -0.15 -> 0.23 -> 0.80 -> 가운데 1.66   (배율 6 적용값)
# 로 아치를 그린다. 도로와 맞춰야 하는 것은 **끝**이지 최고점(1.66, 난간)이 아니다.
# 전에는 수면 + 0.35 에 그냥 얹어 상판이 도로보다 1.5m 높았고, 점프로도
# 못 올라가 다리를 건널 수 없었다.
BRIDGE_DECK_END = -0.15 / 6.0

# 상판 끝이 수면에서 이만큼 위에 오게 한다.
BRIDGE_CLEARANCE = 0.9
# 둑을 돋워 도로를 상판 높이까지 끌어올리는 구간 길이.
BRIDGE_APPROACH = 14.0

TOWN_STREAM_HALF = 5.0    # 수면 반폭 -> 폭 10m
TOWN_STREAM_DEPTH = 1.1   # 수면에서 바닥까지
TOWN_STREAM_BANK = 7.5    # 둑 폭. 둑 기울기를 40도 아래로 유지하는 유일한 손잡이다

# 필드 강 — 북쪽 구릉에서 내려와 가운데 분지의 호수로 들어간다.
# 분지 바닥이 -9m 라 물이 고이는 것이 자연스럽다(내륙 호수).
FIELD_RIVER = [
    (150.0, 36.0),
    (180.0, 80.0),
    (205.0, 120.0),
    (228.0, 160.0),
    (246.0, 194.0),
    (251.0, 205.0),   # 급류 시작
    (256.0, 221.0),   # 급류 끝
    (256.0, 232.0),
]
# 급류. 필드 격자가 8m 라 수직 낙하를 표현할 수 없다 — 한 칸에 담기지 않는다.
# 대신 16m 구간에 6m 를 떨궈 20도 급류로 만든다.
FIELD_RAPIDS = (6, 6.0)
FIELD_LAKE = (256.0, 238.0, 30.0)   # (x, z, 반경)


# 꺾은선을 이 간격으로 잘게 나눈 뒤 수면 높이를 계산한다.
#
# 꼭짓점이 성기면 수면은 두 꼭짓점 사이를 **직선으로** 내려오는데 지형은
# 그렇지 않다. 실제로 z=18 에서 z=58 까지 꼭짓점이 없는 구간에서 지형은
# 7m 떨어지는데 수면은 천천히 내려와, 중간(z=32)에서 수면이 지형보다
# **3.6m 높아졌다.** 물이 골짜기를 벗어나 넓은 단을 덮고 성벽까지 넘어갔다.
STREAM_STEP = 6.0


def _densify(
    points: list[tuple[float, float]], step: float
) -> tuple[list[tuple[float, float]], list[int]]:
    """꺾은선을 촘촘히 나눈다. 원래 꼭짓점이 어디로 갔는지도 함께 돌려준다."""
    out = [points[0]]
    marks = [0]
    for a, b in zip(points, points[1:]):
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        n = max(1, int(math.ceil(length / step)))
        for i in range(1, n + 1):
            t = i / n
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
        marks.append(len(out) - 1)
    return out, marks


def _stream_profile(
    height: Heightmap,
    points: list[tuple[float, float]],
    sink: float,
    grade: float,
    drop: tuple[int, float] | None = None,
) -> tuple[list[tuple[float, float, float]], list[int]]:
    """꺾은선에 수면 높이를 붙인다. (수면 꺾은선, 원래 꼭짓점의 새 인덱스).

    **인덱스를 함께 돌려주는 이유:** 잘게 나누면 원래 3번 꼭짓점이 더 이상
    3번이 아니다. 그걸 잊고 stream[3] 을 다리 자리로 쓰는 바람에 다리가
    엉뚱한 지점(상류)의 수면 높이를 읽었다.


    **물은 반드시 하류로 흐른다.** 지형 높이를 그대로 쓰면 중간에 지형이
    솟은 곳에서 개울이 언덕을 거슬러 오른다. 단조 감소를 강제한다.

    꺾은선은 먼저 STREAM_STEP 간격으로 잘게 나눈다 — 그러지 않으면 수면이
    지형을 앞질러 가 물이 범람한다(위 주석 참조).

    drop 은 (**원래** 꼭짓점 인덱스, 낙차) — 그 구간에서 수면을 뚝 떨어뜨려
    둑을 만든다. 잘게 나눈 뒤에도 원래 꼭짓점을 추적해 같은 자리에 적용한다.
    """
    dense, marks = _densify(points, STREAM_STEP)
    drop_at = marks[drop[0]] if drop is not None else -1

    out: list[tuple[float, float, float]] = []
    prev = None
    for i, (x, z) in enumerate(dense):
        y = height.sample(x, z) - sink
        if prev is not None:
            dist = math.hypot(x - dense[i - 1][0], z - dense[i - 1][1])
            y = min(y, prev - grade * dist)
            if i == drop_at:
                y = prev - drop[1]          # type: ignore[index]
        out.append((x, z, y))
        prev = y
    return out, marks


def _reserve_along(lay: Layout, points: list[tuple[float, float, float]], radius: float) -> None:
    """꺾은선을 따라 영역을 막는다. 물 위에 건물이 서지 않게 한다."""
    for a, b in zip(points, points[1:]):
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        n = max(1, int(length / (radius * 0.7)))
        for i in range(n + 1):
            t = i / n
            lay.reserve(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, radius)


NPC_IDLE: dict[str, tuple[str, tuple[str, ...]]] = {
    # 위병은 부동자세. 가끔 자세만 고쳐 선다.
    "guard": ("Idle_A", ("Idle_B",)),
    # 상인은 물건을 만지작거린다.
    "merchant": ("Interact", ("Idle_B", "PickUp")),
    # 주민은 가장 부산하게.
    "villager": ("Idle_B", ("Idle_A", "PickUp", "Use_Item", "Interact")),
}


def _angle_diff(a: float, b: float) -> float:
    """두 각의 차를 -pi..pi 로 접는다. 원형 배치에서 구간을 건너뛸 때 쓴다."""
    return (a - b + math.pi) % math.tau - math.pi


def _stair_count(span: float, tile_width: float) -> int:
    """계단을 몇 틀 늘어놓을지. **항상 홀수**로 만든다.

    짝수면 가운데에 틀 경계가 오고, 계단 모델의 옆 난간 두 개가 거기서 맞닿아
    통행로 한복판에 벽이 선다. 실제로 성 계단이 2틀이라 도로 중심(x=128)에
    난간 벽이 생겨 올라갈 수 없었다.
    """
    n = max(1, int(round(span / tile_width)))
    return n if n % 2 == 1 else n + 1


# ----------------------------------------------------------------------------
# 에셋 치수 조회
# ----------------------------------------------------------------------------


class AssetTable:
    """manifest + 실측 바운딩 박스. 배율이 적용된 월드 단위 치수를 돌려준다."""

    def __init__(self, repo: Path):
        self.repo = repo
        self.manifest = load_manifest(repo)
        self._bounds: dict[str, Bounds | None] = {}

    def entry(self, asset_id: str) -> dict:
        return self.manifest["assets"][asset_id]

    def scale(self, asset_id: str) -> float:
        return float(self.entry(asset_id).get("scale", 1.0))

    def bounds(self, asset_id: str) -> Bounds | None:
        if asset_id not in self._bounds:
            self._bounds[asset_id] = read_bounds(
                asset_path(self.repo, self.manifest, asset_id)
            )
        return self._bounds[asset_id]

    def radius(self, asset_id: str) -> float:
        """배율을 적용한 XZ 외접 반경. 간격 판정의 기준."""
        b = self.bounds(asset_id)
        if b is None:
            return 1.0
        return b.footprint_radius * self.scale(asset_id)

    def width_x(self, asset_id: str) -> float:
        b = self.bounds(asset_id)
        return 1.0 if b is None else b.size[0] * self.scale(asset_id)

    def depth_z(self, asset_id: str) -> float:
        b = self.bounds(asset_id)
        return 1.0 if b is None else b.size[2] * self.scale(asset_id)

    def y_offset(self, asset_id: str) -> float:
        """manifest 가 지정한 보정값에 배율을 곱한다. 지면에 묻히는 모델용."""
        return float(self.entry(asset_id).get("y_offset", 0.0)) * self.scale(asset_id)


# ----------------------------------------------------------------------------
# 배치 누적기 — 겹침 판정과 지면 스냅을 한 곳에서 한다
# ----------------------------------------------------------------------------


class Layout:
    def __init__(self, table: AssetTable, height: Heightmap):
        self.table = table
        self.height = height
        self.placements: list[dict] = []
        # (x, z, radius) — 점유 원. 무거운 자료구조를 쓰지 않는다;
        # 이 규모(수천 개)에서는 격자 해싱으로 충분하다.
        self._circles: list[tuple[float, float, float]] = []
        self._grid: dict[tuple[int, int], list[int]] = {}
        self._cell = 16.0

    # --- 겹침 판정 -----------------------------------------------------------

    def _cells(self, x: float, z: float, r: float):
        x0, x1 = int((x - r) // self._cell), int((x + r) // self._cell)
        z0, z1 = int((z - r) // self._cell), int((z + r) // self._cell)
        for cx in range(x0, x1 + 1):
            for cz in range(z0, z1 + 1):
                yield (cx, cz)

    def blocked(self, x: float, z: float, r: float) -> bool:
        seen: set[int] = set()
        for cell in self._cells(x, z, r):
            for i in self._grid.get(cell, ()):
                if i in seen:
                    continue
                seen.add(i)
                ox, oz, orr = self._circles[i]
                if (x - ox) ** 2 + (z - oz) ** 2 < (r + orr) ** 2:
                    return True
        return False

    def reserve(self, x: float, z: float, r: float) -> None:
        """배치물 없이 영역만 막는다 — 도로, 광장, 스폰 주변 등."""
        index = len(self._circles)
        self._circles.append((x, z, r))
        for cell in self._cells(x, z, r):
            self._grid.setdefault(cell, []).append(index)

    # --- 배치 ---------------------------------------------------------------

    def ground_y(self, asset_id: str, x: float, z: float) -> float:
        """5장의 '지면 높이 스냅'.

        종류에 따라 기준이 다르다:

        - 건물은 덩어리라 발자국 안 **최저점**에 맞춰야 낮은 쪽 모서리가
          공중에 뜨지 않는다. 높은 쪽이 조금 묻히는 건 자연스럽다.
        - 포석·계단 같은 타일은 얇고 평평하다. 최저점에 맞추면 경사면에서
          오르막 모서리가 지형에 묻혀 도로가 얼룩말 무늬가 된다.
          **최고점**에 맞춰 내리막 쪽에 몇 cm 턱을 남기는 편이 훨씬 낫다.
        - 나무·바위는 줄기나 밑동이 중심에 있고 반경은 수관이 잡아먹는다.
          최저점을 쓰면 반경 11m 짜리 나무가 3m 씩 땅에 묻힌다. 중심 높이를 쓴다.
        """
        kind = self.table.entry(asset_id).get("kind", "prop")
        r = self.table.radius(asset_id)
        if kind == "building" and r > 1.0:
            return self.height.footprint_min(x, z, r * 0.8)
        if kind == "tile" and r > 0.5:
            # 사각 타일의 모서리까지 닿도록 반경을 조금 키워 잡되,
            # 옆에 단차나 급경사가 있으면 그 높이를 물어 타일이 통째로 떠오른다
            # (실제로 성 언덕 58% 사면에서 2.2m 가 떴다). 상한을 둔다.
            base = self.height.sample(x, z)
            return min(self.height.footprint_max(x, z, r * 1.4), base + 0.3)
        return self.height.sample(x, z)

    def place(
        self,
        asset_id: str,
        x: float,
        z: float,
        yaw: float = 0.0,
        *,
        radius: float | None = None,
        y: float | None = None,
        y_bias: float = 0.0,
        scale: float | None = None,
    ) -> dict:
        entry = self.table.entry(asset_id)
        r = self.table.radius(asset_id) if radius is None else radius
        base = self.ground_y(asset_id, x, z) if y is None else y
        used_scale = self.table.scale(asset_id) if scale is None else scale

        item = {
            "asset": asset_id,
            "pos": [
                round(x, 3),
                round(base + self.table.y_offset(asset_id) + y_bias, 3),
                round(z, 3),
            ],
            "yaw": round(yaw, 4),
            "scale": round(used_scale, 4),
            "collision": entry.get("collision", "none"),
            "nav": entry.get("nav", "include"),
        }
        self.placements.append(item)
        self.reserve(x, z, r)
        return item

    def try_place(
        self,
        asset_id: str,
        x: float,
        z: float,
        yaw: float = 0.0,
        *,
        max_slope: float | None = None,
    ) -> bool:
        r = self.table.radius(asset_id)
        if self.blocked(x, z, r):
            return False
        if max_slope is not None and self.height.slope_at(x, z) > max_slope:
            return False
        self.place(asset_id, x, z, yaw)
        return True


# ----------------------------------------------------------------------------
# 마을 (256 x 256) — 평화지역
# ----------------------------------------------------------------------------

TOWN_WALL_MIN, TOWN_WALL_MAX = 32.0, 224.0
TOWN_MID = TOWN_SIZE / 2.0

# 광장 대지 — 도로에서 3m 올라가고 치맛자락이 4m 라 37도다. 걸어 오를 수 없으니
# 네 접근로마다 계단을 놓는다. 이게 '경사 + 계단'이 데이터로 생기는 지점이다.
#
# 사각으로 두는 이유: 포석이 4m 사각 타일이라 원형 대지에 깔면 경계가
# 톱니로 보인다. 반폭을 타일 크기의 배수(24 = 4 x 6)로 잡아 딱 떨어지게 한다.
PLAZA_HALF = 24.0
PLAZA = BoxPlateau(
    x=TOWN_MID, z=TOWN_MID, half_x=PLAZA_HALF, half_z=PLAZA_HALF, skirt=4.0, height=3.0
)
# 성 언덕 — 광장보다 4m 더 높다.
#
# z 를 78 에 두면 치맛자락(78+19+13 = 110)이 광장 치맛자락(100)과 10m 겹쳐
# 성 계단(z=103.5)과 광장 북쪽 계단(z=102)이 1.5m 간격으로 포개졌다.
# 두 대지 사이에 평지를 남기도록 북쪽으로 밀었다.
CASTLE_MOUND = Plateau(x=TOWN_MID, z=62.0, radius=17.0, skirt=12.0, height=7.0)


def town_heightmap(rng: random.Random) -> Heightmap:
    return build_heightmap(
        TOWN_SIZE,
        # 2m 격자. 도로를 지면에 칠하므로 경계가 또렷하려면 촘촘해야 한다.
        2.0,
        rng,
        bumps=[
            # 성벽 밖 구릉. 지평선이 평평하지 않게 한다.
            # 마을 밖 여유가 32m 뿐이라 중심을 모서리로 밀어 봉우리만 걸치게 한다.
            Bump(-10.0, 10.0, 96.0, 20.0),
            Bump(268.0, 4.0, 92.0, 18.0),
            Bump(-14.0, 250.0, 88.0, 15.0),
            Bump(272.0, 262.0, 94.0, 17.0),
            Bump(128.0, -16.0, 110.0, 14.0),
        ],
        plateaus=[PLAZA, CASTLE_MOUND],
        # 성벽 안에서도 눌리지 않는다. 대로가 북쪽으로 완만히 오르게 한다.
        global_bumps=[Bump(TOWN_MID, 40.0, 190.0, 4.0)],
        # 성벽 안은 평탄하게 눌러야 건물이 기울지 않는다.
        flatten_box=(TOWN_WALL_MIN, TOWN_WALL_MAX, TOWN_WALL_MIN, TOWN_WALL_MAX),
        flatten_skirt=14.0,
        noise_amplitude=1.5,
        noise_scale=46.0,
    )


def build_town(table: AssetTable, rng: random.Random) -> dict:
    size = TOWN_SIZE
    mid = TOWN_MID
    wall_min, wall_max = TOWN_WALL_MIN, TOWN_WALL_MAX

    height = town_heightmap(rng)

    road_half = 6.0      # 대로 반폭 → 폭 12m
    tile = 4.0
    plaza_r = PLAZA_HALF

    # ------------------------------------------------------------------
    # 지형 깎기는 **전부 배치보다 먼저** 끝낸다.
    # 배치 y 는 놓는 시점의 지형에서 뽑으므로, 나중에 지형을 건드리면
    # 이미 놓인 것들이 뜨거나 묻힌다.
    # ------------------------------------------------------------------

    # 도로 노반. 이걸 하지 않으면 경사면에서 타일마다 지형에 물려
    # 4m 간격으로 검은 줄이 생긴다.
    height.grade_corridor(mid, wall_min - 10, mid, wall_max + 10, road_half + 3.0, feather=7.0)
    height.grade_corridor(wall_min - 10, mid, wall_max + 10, mid, road_half + 3.0, feather=7.0)

    # 계단 치수와 발치 평탄화.
    #
    # 배율은 manifest 값을 그대로 쓰지 않고 **실제 단차에서 역산한다.**
    # 계단 모델의 원점은 윗단이다 (local z [0, 8] 에서 z=0 쪽이 높다).
    # 원점을 광장 가장자리에 놓으면 윗단이 광장과 맞물리고 경사로가 바깥으로 내려간다.
    stairs_raw = table.bounds("tile_stairs_long")
    assert stairs_raw is not None
    raw_rise, raw_width, raw_depth = stairs_raw.size  # 5.00 x 5.10 x 8.00
    raw_rise, raw_width, raw_depth = stairs_raw.size[1], stairs_raw.size[0], stairs_raw.size[2]

    def fit_stairs(bottom_y: float, top_y: float) -> tuple[float, float]:
        """단차를 정확히 메우는 (배율, 배율 적용 폭) 을 돌려준다."""
        s = max(0.25, (top_y - bottom_y) / raw_rise)
        return s, raw_width * s

    plaza_y = height.sample(mid, mid)
    stair_ring = PLAZA_HALF - 0.3

    # 접근 방향. 계단 경사로는 광장 바깥(= 접근 방향)으로 내려가야 한다.
    #
    # 모델 로컬 +Z 가 내리막이고, yaw 회전 뒤 (sin yaw, cos yaw) 를 향한다.
    # 따라서 yaw = atan2(dx, dz) 다. 손으로 적었다가 동/서의 부호를 뒤집어
    # 두 곳의 경사로가 광장 **안쪽**으로 내려가 단 속에 묻혀 있었다.
    approach_dirs = [
        (0.0, 1.0),    # 남쪽에서 접근
        (0.0, -1.0),   # 북쪽에서
        (1.0, 0.0),    # 동쪽에서
        (-1.0, 0.0),   # 서쪽에서
    ]
    approaches = [((dx, dz), math.atan2(dx, dz)) for dx, dz in approach_dirs]

    # 네 발치를 모두 광장 설계 높이(상판 - 3.0m)로 맞춘다.
    #
    # 지형에서 그때그때 읽으면 접근로마다 단차가 달라진다. 북쪽은 성 언덕이
    # 땅을 밀어올려 단차가 1.5m 밖에 안 됐고, 그러면 계단 길이가 2.36m 로
    # 줄어 격자 한 칸(4m)에 걸친 단차 보간 구간을 덮지 못해 발치가 묻혔다.
    # 높이를 통일하면 계단 4개가 같아지고 지형 경사와도 기울기가 맞는다.
    base_y = plaza_y - PLAZA.height

    stair_plan: list[tuple[float, float, float, float, float, float, float]] = []
    for (dx, dz), yaw in approaches:
        s, w = fit_stairs(base_y, plaza_y)
        depth = raw_depth * s
        count = _stair_count(road_half * 2.0, w)

        # 계단 밑을 **직선 경사로** 깎는다.
        #
        # 평평하게 깎으면 광장 가장자리가 수직 절벽이 되고 그 밖은 평지라,
        # 계단이 평지 위에 다리처럼 걸쳐 최대 3m 떠 보인다.
        # 성 언덕 계단과 같은 처리다.
        # 계단보다 넉넉히 넓게 깎아야 계단이 언덕에 파묻히지 않는다.
        # 계단 폭에 딱 맞추면 바깥 틀이 feather 구간에 걸려 옆구리가 묻힌다.
        ramp_half = w * count * 0.5 + 2.5
        height.grade_ramp(
            mid + dx * PLAZA_HALF, mid + dz * PLAZA_HALF, plaza_y,
            mid + dx * (PLAZA_HALF + depth), mid + dz * (PLAZA_HALF + depth), base_y - STAIR_CUT,
            half_width=ramp_half,
            feather=3.0,
        )
        # 경사로 끝에서 주변 지형으로 이어 붙인다. 이 구간이 없으면
        # 램프 끝에 턱이 남는다.
        # 완만할수록 평면 타일과 지형의 틈이 작아진다 (틈 = 경사 x 타일크기).
        blend = 16.0
        outer = height.sample(
            mid + dx * (PLAZA_HALF + depth + blend),
            mid + dz * (PLAZA_HALF + depth + blend))
        height.grade_ramp(
            mid + dx * (PLAZA_HALF + depth), mid + dz * (PLAZA_HALF + depth), base_y - STAIR_CUT,
            mid + dx * (PLAZA_HALF + depth + blend),
            mid + dz * (PLAZA_HALF + depth + blend), outer,
            half_width=ramp_half,
            feather=3.0,
        )
        stair_plan.append(
            (mid + dx * stair_ring, mid + dz * stair_ring, yaw, base_y, s, w, depth)
        )

    # 성 언덕 계단 — 광장 북쪽에서 오른다.
    #
    # 여기는 발치를 파지 않는다. 치맛자락을 파내면 언덕 가장자리가 절벽이 되고
    # (격자 한 칸에 6.2m 낙차) 계단 윗단이 붙을 경사면이 사라져 계단이
    # 4.2m 떠 버렸다. 대신 **계단을 자연 경사면에 그대로 얹는다.**
    #
    # 치맛자락 기울기 7.0m / 12m = 58% 와 계단 기울기 5.10 / 8.00 = 64% 가
    # 거의 같아서 잘 맞는다. 계단 상승을 대지 높이에 맞추면 윗단은 가장자리,
    # 밑단은 치맛자락 끝에 닿는다.
    mound_top_y = height.sample(CASTLE_MOUND.x, CASTLE_MOUND.z)
    mound_edge = CASTLE_MOUND.z + CASTLE_MOUND.radius

    # 계단 길이와 발치 높이는 서로 물려 있다(등방 배율). 두 번 돌려 수렴시킨다.
    mound_depth = CASTLE_MOUND.skirt
    for _ in range(2):
        mound_base_y = height.sample(CASTLE_MOUND.x, mound_edge + mound_depth)
        ms, mw = fit_stairs(mound_base_y, mound_top_y)
        mound_depth = raw_depth * ms

    mound_count = _stair_count(14.0, mw)

    # 계단이 놓일 띠를 직선으로 깎아 S 자 치맛자락과의 어긋남을 없앤다.
    #
    # 계단면과 지형을 같은 높이로 두면 두 면이 겹쳐 z-fighting 이 나고
    # 디딤판이 잔디에 먹힌다(난간만 보였다). 램프를 조금 낮춰 계단을 띄운다.
    ramp_sink = STAIR_CUT
    height.grade_ramp(
        CASTLE_MOUND.x, mound_edge, mound_top_y - ramp_sink,
        CASTLE_MOUND.x, mound_edge + mound_depth, mound_base_y - ramp_sink,
        half_width=mw * mound_count * 0.5 + 2.5,
        feather=4.0,
    )
    # 램프의 feather 가 상판을 건드렸을 수 있으니 다시 고정한다.
    height.grade_disc(
        CASTLE_MOUND.x, CASTLE_MOUND.z, CASTLE_MOUND.radius, mound_top_y, feather=0.0
    )

    # 발치 평탄화의 feather 가 광장 안쪽까지 파고들어 가장자리를 끌어내린다.
    # (그 위에 놓은 난간·가로등이 최대 1.96m 떴다.)
    # 광장 상판을 feather 없이 다시 눌러 경계를 또렷하게 되돌린다.
    # 이러면 광장과 발치 사이에 수직 단차가 남는데, 계단이 놓이는 자리가 바로 거기다.
    height.grade_flat(mid, mid, PLAZA_HALF, PLAZA_HALF, plaza_y, feather=0.0)

    # 마지막으로 **걸어 오를 수 없는 급경사를 전부 편다.**
    #
    # 대지 가장자리를 또렷하게 만드느라 3m 단차를 격자 한 칸(2m)에 떨궜는데,
    # 그러면 56도가 되어 Godot 이 그 면을 바닥이 아니라 벽으로 분류한다.
    # 걸어 오를 수 없으니 step-up 이 개입해 캐릭터를 들어 올렸다 — 순간이동.
    # 광장 둔덕이 계단보다 높아 보이던 것도 같은 원인이다.
    before = height.max_slope()
    height.limit_slope(MAX_WALK_SLOPE)
    after = height.max_slope()
    print(f"  경사 정리: 최대 tan {before:.2f}({math.degrees(math.atan(before)):.0f}도)"
          f" -> {after:.2f}({math.degrees(math.atan(after)):.0f}도)   걸을 수 있는 한계 52도")

    # --- 개울 -----------------------------------------------------------------
    #
    # **경사 정리(limit_slope) 뒤에** 판다. 먼저 파면 둑이 도로 펴져서 개울이
    # 메워진다. 대신 여기서 만드는 경사는 아무도 검사하지 않으므로
    # depth/bank 비율로 **직접** 40도 아래를 보장해야 한다.
    stream, stream_marks = _stream_profile(
        height, TOWN_STREAM, sink=0.5, grade=0.008, drop=TOWN_WEIR)
    # 원래 꼭짓점 번호로 지점을 고른다. 잘게 나눈 뒤 인덱스가 밀리기 때문이다.
    node = lambda k: stream[stream_marks[k]]
    height.carve_channel(
        stream, TOWN_STREAM_HALF, TOWN_STREAM_DEPTH, TOWN_STREAM_BANK)

    # --- 다리 둑 --------------------------------------------------------------
    #
    # 다리를 물 위에 얹기만 하면 상판이 도로보다 높이 솟아 건널 수 없다.
    # **도로를 상판 높이까지 끌어올린다** — 실제 다리의 교대(abutment)와 같다.
    # 개울을 판 뒤에 해야 한다. 먼저 하면 물길 파기가 도로 둑을 도로 깎는다.
    bridge_x, _, bridge_water = node(3)
    deck_y = bridge_water + BRIDGE_CLEARANCE
    half_span = table.width_x("prop_bridge") * 0.5          # 12m / 2
    bridge_depth = table.depth_z("prop_bridge")             # 4.46m, 세 틀을 늘어놓는다
    for side in (-1.0, 1.0):
        inner_x = bridge_x + side * half_span
        outer_x = bridge_x + side * (half_span + BRIDGE_APPROACH)
        height.grade_ramp_span(
            inner_x, mid, deck_y,
            outer_x, mid, height.sample(outer_x, mid),
            half_width=bridge_depth * 1.7,
            feather=4.0,
            # 앞뒤로는 바짝 끊는다. 여유를 주면 둑이 물길 안쪽까지 먹어
            # 폭 10m 개울이 2m 로 졸아든다(실측).
            feather_along=0.6,
        )

    # 지형을 다 만진 **뒤에** 수면을 마무리한다.
    water_points = height.finish_water()
    print(f"  개울: 수면 {stream[0][2]:.1f}m -> {stream[-1][2]:.1f}m,"
          f" 둑 낙차 {TOWN_WEIR[1]:.1f}m, 다리 상판 {deck_y:.2f}m,"
          f" 물 격자점 {water_points}, 수면 구멍 {height.water_holes()}")

    # 계단을 **완화가 끝난 최종 지형**에서 다시 맞춘다.
    #
    # 경사를 펴면 광장 가장자리가 내려앉고 발치는 올라온다. 펴기 전 값으로
    # 계단을 놓으면 윗단이 가라앉은 가장자리보다 솟아 "광장 둔덕이 계단보다
    # 높다"는 모양이 된다. 지형을 다시 읽어 배율과 발치 높이를 재계산한다.
    #
    # 발치는 지형보다 STAIR_CUT 만큼 띄운다 — 계단면과 지면이 같은 높이면
    # z-fighting 으로 디딤판이 잔디에 먹힌다.
    refined: list[tuple[float, float, float, float, float, float, float]] = []
    for (dx, dz), yaw in approaches:
        cx, cz = mid + dx * stair_ring, mid + dz * stair_ring
        top_y = height.sample(cx, cz)
        depth = raw_depth * fit_stairs(top_y - PLAZA.height, top_y)[0]
        # 길이와 발치 높이가 서로 물려 있다(등방 배율). 두 번 돌려 수렴시킨다.
        for _ in range(2):
            foot_y = height.sample(
                mid + dx * (PLAZA_HALF + depth), mid + dz * (PLAZA_HALF + depth)
            ) + STAIR_CUT
            s, w = fit_stairs(foot_y, top_y)
            depth = raw_depth * s
        refined.append((cx, cz, yaw, foot_y, s, w, depth))
    stair_plan = refined

    # 성 언덕 계단도 같은 이유로 다시 맞춘다. 윗단이 닿을 곳은 대지 상판이
    # 아니라 **램프를 깎아 놓은 가장자리**이므로 거기를 읽고 sink 를 되돌린다.
    mound_top_y = height.sample(CASTLE_MOUND.x, mound_edge) + ramp_sink
    for _ in range(2):
        mound_base_y = height.sample(CASTLE_MOUND.x, mound_edge + mound_depth) + ramp_sink
        ms, mw = fit_stairs(mound_base_y, mound_top_y)
        mound_depth = raw_depth * ms
    mound_count = _stair_count(14.0, mw)

    lay = Layout(table, height)

    # 개울 위에는 아무것도 놓지 않는다. 배치보다 **먼저** 막아야 한다.
    _reserve_along(lay, stream, TOWN_STREAM_HALF + TOWN_STREAM_BANK * 0.5)

    # --- 도로와 광장 ----------------------------------------------------------
    #
    # **타일 모델을 깔지 않고 지면에 칠한다.**
    #
    # 평평한 4m 타일을 경사에 놓으면 낮은 쪽 모서리가 (경사 x 타일크기) 만큼 뜬다.
    # 실측 최대 0.94m 였고, 그 턱마다 캐릭터가 step-up 으로 순간이동했다.
    # 타일을 작게 쪼개도 틈이 비례해서 줄 뿐 없어지지 않는다.
    #
    # 지면 자체에 포석 색을 칠하면 틈도 턱도 원천적으로 생기지 않는다.
    # 콜리전도 지형 하나로 끝나고 노드도 300개 넘게 줄어든다.
    road_half_paint = road_half

    # 대로 — 성 언덕 위는 칠하지 않는다. 대로는 성 계단 앞에서 끝난다.
    height.paint_corridor(mid, wall_min - 4, mid, CASTLE_MOUND.z + CASTLE_MOUND.radius + 6,
                          road_half_paint, feather=1.5)
    height.paint_corridor(mid, mid, mid, wall_max + 4, road_half_paint, feather=1.5)
    height.paint_corridor(wall_min - 4, mid, wall_max + 4, mid, road_half_paint, feather=1.5)

    # 광장 — 대지 전체를 칠한다. 가장자리는 또렷하게.
    height.paint_box(mid, mid, PLAZA_HALF, PLAZA_HALF, feather=1.0)

    # --- 광장 진입 계단 --------------------------------------------------------
    # 지형은 위에서 이미 깎아 뒀다. 여기서는 놓기만 한다.
    #
    # 배율은 단차에서 나오고 배율이 폭까지 함께 바꾼다(등방 확대). 따라서
    # 개수를 고정하면 계단 전체 폭이 접근로마다 달라진다 — 실제로 북쪽 5.9m,
    # 남쪽 17.4m 가 나왔고 도로는 12m 였다.
    # **개수를 계산해 폭을 맞춘다.** 배치 스키마가 균일 배율 하나뿐이라 이 방법뿐이다.
    stair_span = road_half * 2.0
    for cx, cz, yaw, base_y, s, w, depth in stair_plan:
        # 진행 방향과 직각으로 늘어놓아 도로 폭을 채운다.
        px, pz = (1.0, 0.0) if abs(cz - mid) > abs(cx - mid) else (0.0, 1.0)
        count = _stair_count(stair_span, w)
        for i in range(count):
            k = i - (count - 1) / 2.0
            lay.place(
                "tile_stairs_long",
                cx + px * w * k,
                cz + pz * w * k,
                yaw,
                radius=w * 0.45,
                y=base_y,
                scale=s,
            )
        lay.reserve(cx, cz, max(stair_span * 0.6, depth))

    for x0, z0, x1, z1 in ((mid, wall_min, mid, wall_max), (wall_min, mid, wall_max, mid)):
        length = math.hypot(x1 - x0, z1 - z0)
        n = int(length / road_half) + 1
        for i in range(n + 1):
            t = i / n
            lay.reserve(x0 + (x1 - x0) * t, z0 + (z1 - z0) * t, road_half)
    lay.reserve(mid, mid, plaza_r)

    # --- 성벽 -----------------------------------------------------------------
    seg = table.width_x("bld_wall_straight")  # 2.00 * 6.0 = 12.0 m
    span = wall_max - wall_min
    count = int(round(span / seg))
    step = span / count

    # 조각 중심을 wall_min + step*i 로 잡는다. (i + 0.5) 로 두면 중심이
    # 122 와 134 에 와서 도로 중심(128)에는 조각 **경계**가 걸린다.
    # 그러면 성문이 도로에서 6m 비껴 서고 통로가 벽으로 막힌다.
    # i 로 잡으면 양 끝(모서리)과 한가운데가 모두 조각 중심이 된다.
    gate_index = count // 2

    # 성벽이 개울을 가로지르는 자리는 **비운다.**
    #
    # 성벽 조각을 그대로 세우면 물이 벽을 뚫고 흐른다. 성문 아치를 쓰려고
    # 재 봤더니 통로가 5.2m 라 폭 10m 개울이 안 들어간다. 벽을 물 아래까지
    # 늘리면 개울을 막아 버린다.
    #
    # 강이 곧 방벽이 되는 배치는 실제 축성에서도 흔하다. 빈 자리 양옆을
    # 감시탑으로 막아 "무너진 곳"이 아니라 "일부러 낸 수문"으로 보이게 한다.
    def stream_gap(x: float, z: float) -> bool:
        """이 성벽 조각이 개울에 걸치는가."""
        clear = step * 0.5 + TOWN_STREAM_HALF
        for a, b in zip(stream, stream[1:]):
            dx, dz = b[0] - a[0], b[1] - a[1]
            seg = dx * dx + dz * dz
            if seg <= 0.0:
                continue
            t = max(0.0, min(1.0, ((x - a[0]) * dx + (z - a[1]) * dz) / seg))
            if math.hypot(x - (a[0] + dx * t), z - (a[1] + dz * t)) < clear:
                return True
        return False

    def wall_run(fixed: float, yaw: float, axis: str) -> None:
        # 양 끝(모서리)은 건너뛴다. 거기엔 감시탑이 선다.
        #
        # 전에는 네 모서리마다 **직선벽 두 조각(가로줄·세로줄)과 감시탑이 같은
        # 좌표에 겹쳐** 있었다(실측 거리 0.0m). 탑을 뚫고 나온 흰 벽이 그것이다.
        # 탑 반경 5.28m 에 첫 조각이 6.0m 앞에서 시작하므로 틈은 0.7m 남는데,
        # 캐릭터 캡슐 지름이 0.84m 라 빠져나갈 수 없다.
        spots = []
        for i in range(1, count):
            t = wall_min + step * i
            x, z = (t, fixed) if axis == "x" else (fixed, t)
            spots.append((i, x, z, stream_gap(x, z)))

        for idx, (i, x, z, gap) in enumerate(spots):
            if gap:
                continue
            # 빈 자리와 맞닿은 조각은 감시탑으로 바꿔 끝을 마감한다.
            flank = ((idx > 0 and spots[idx - 1][3])
                     or (idx + 1 < len(spots) and spots[idx + 1][3]))
            if flank:
                lay.place("bld_watchtower", x, z, yaw, radius=step * 0.45)
            else:
                asset = "bld_wall_gate" if i == gate_index else "bld_wall_straight"
                lay.place(asset, x, z, yaw, radius=step * 0.45)

    wall_run(wall_min, 0.0, "x")
    wall_run(wall_max, math.pi, "x")
    wall_run(wall_min, math.pi / 2, "z")
    wall_run(wall_max, -math.pi / 2, "z")

    # 모서리는 감시탑으로 마감한다. 코너 조각(1.38)과 직선(2.00)은 모듈이 안 맞는다.
    for cx in (wall_min, wall_max):
        for cz in (wall_min, wall_max):
            lay.place("bld_watchtower", cx, cz, rng.uniform(0, math.tau))

    # --- 개울 위의 것들 ---------------------------------------------------------
    #
    # 물레방아를 뒷골목에 흩어 놓던 것을 그만두고 **개울가에 세운다.**
    # 물 없이 도는 물레방아가 제일 어색했다.
    #
    # 모델 로컬 -X 쪽에 바퀴가 달려 있다(바퀴 노드 translation.x = -0.187,
    # 배율 6 이면 -1.1m). yaw=0 이면 그 방향이 월드 -X 다. 따라서 개울
    # **동쪽 둑**에 놓고 yaw=0 으로 두면 바퀴가 물에 잠긴다.
    for idx in (2, 5, 6):
        wx, wz, wy = node(idx)
        # 바퀴 중심이 수면 가장자리 안쪽에 오도록 원점을 잡는다.
        lay.place("bld_watermill", wx + TOWN_STREAM_HALF + 1.2, wz, 0.0, y=wy - 0.3)

    # 대로가 개울을 건너는 자리(z = mid)에 다리. 모델이 X 로 12m 긴데 도로는
    # 폭 12m 라, Z 로 나란히 세 틀 놓아 도로 폭을 채운다.
    #
    # 원점 높이는 **상판 끝이 도로와 같아지도록** 역산한다. 위에서 도로를
    # deck_y 까지 끌어올려 놨으므로 여기서 맞추면 턱이 0 이 된다.
    bridge_origin_y = deck_y - BRIDGE_DECK_END * table.scale("prop_bridge")
    for k in (-1, 0, 1):
        lay.place("prop_bridge", bridge_x, mid + k * bridge_depth, 0.0,
                  radius=bridge_depth * 0.45, y=bridge_origin_y)

    # --- 랜드마크 --------------------------------------------------------------
    lay.place("bld_well", mid, mid, 0.0)
    lay.place("bld_castle", CASTLE_MOUND.x, CASTLE_MOUND.z, math.pi)

    # 성 언덕으로 오르는 계단. 발치는 위에서 이미 평탄화했고 개수도 정해 뒀다.
    for i in range(mound_count):
        k = i - (mound_count - 1) / 2.0
        lay.place(
            "tile_stairs_long",
            CASTLE_MOUND.x + k * mw,
            mound_edge,
            0.0,
            radius=mw * 0.45,
            y=mound_base_y,
            scale=ms,
        )
    lay.reserve(CASTLE_MOUND.x, mound_edge, mw * 2.0)

    # 상점은 광장 북서쪽 모서리 바깥. 상인 NPC 가 그 앞에 선다.
    #
    # 전에는 (mid - plaza_r - 12, mid) 즉 서쪽 대로 한복판이었다. 그 자리는
    # 서쪽 진입 계단 발치라 상인이 계단에 끼어 있었다. 도로 축에서 비켜 놓는다.
    market_x, market_z = mid - plaza_r - 10.0, mid - plaza_r + 4.0
    lay.place("bld_shop_market", market_x, market_z, math.pi / 2)

    # 성문 안쪽 — 깃발은 벽에 매다는 모델이라(min.y = +0.53) 세울 벽이 없으면
    # 허공에 뜬다. 실제로 2m 떠 있었다. 가로등으로 대체한다.
    for gx in (mid - 8.0, mid + 8.0):
        lay.place("prop_lamp_post", gx, wall_max - 5.0, 0.0)

    # --- 건물 ------------------------------------------------------------------
    placed_buildings = _town_buildings(lay, rng, table)

    # --- 소품 ------------------------------------------------------------------
    _town_props(lay, rng, table)

    # --- 굴뚝 연기 ---------------------------------------------------------------
    _mark_smoke(lay, rng)

    # --- NPC -------------------------------------------------------------------
    npcs = _town_npcs(lay, market_x, market_z, mid, wall_max)

    # --- 밭과 식생 --------------------------------------------------------------
    scatter(lay, rng, ["bld_farm_plot"], 30, 36, size - 36, wall_max + 12, size - 10,
            max_slope=0.25)

    garden = ["prop_med_tree_a", "prop_med_tree_b", "prop_med_tree_c"]
    ground_cover = [
        "prop_flower_group_01", "prop_flower_group_02", "prop_clover_01",
        "prop_grass_short_01", "prop_grass_wispy_01", "prop_mushroom_01",
    ]
    outside = [
        "prop_tree_pine_01", "prop_tree_pine_02", "prop_tree_pine_03",
        "prop_tree_common_02", "prop_tree_common_03", "prop_tree_common_05",
        "prop_rock_medium_01", "prop_rock_medium_02", "prop_med_rocks",
    ]

    scatter(lay, rng, garden, 90, wall_min + 8, wall_max - 8, wall_min + 8, wall_max - 8)
    scatter(lay, rng, ground_cover, 700, wall_min + 4, wall_max - 4, wall_min + 4, wall_max - 4)
    scatter(lay, rng, outside, 520, 4, size - 4, 4, size - 4,
            exclude_box=(wall_min - 8, wall_max + 8, wall_min - 8, wall_max + 8))
    scatter(lay, rng, ground_cover + ["prop_plant_big_01", "prop_med_rocks_small"], 420,
            4, size - 4, 4, size - 4,
            exclude_box=(wall_min - 8, wall_max + 8, wall_min - 8, wall_max + 8))

    return {
        "schema_version": 2,
        "id": "town",
        "display_name": "마을",
        "generated": date.today().isoformat(),
        "generator": "tools/gen_layout.py",
        "seed": SEED,
        "origin": [0.0, 0.0],
        "size": [size, size],
        "y_range": [0, 64],
        "sector_size": 32,
        "terrain": height.to_json(),
        "ground": {"color": "#54763c", "road_color": "#a8a49b", "nav": "include"},
        # 물은 **걸어 다닐 수 없는 영역**이다 — Week 2 Recast 빌드가 이 값을
        # 읽어 내비메시에서 뺀다(5장의 nav 필드와 같은 규약). 내비메시가
        # 무언가를 증명하려면 통과 못 하는 영역이 실제로 있어야 한다.
        "water": {
            "color": "#3d7a92", "deep_color": "#15414f", "foam_color": "#dff1f5",
            "nav": "exclude",
            "cells": height.water_cells(),
        },
        # CLAUDE.md 9장 — 평화지역. 판정은 서버에서만 한다.
        "flags": {"pvp": False, "spawn": False},
        "spawn_point": [mid, round(height.sample(mid, wall_max - 14.0), 3), wall_max - 14.0],
        "npcs": npcs,
        "spawns": [],
        "patrol_routes": [],
        "placements": lay.placements,
        "stats": {"buildings": placed_buildings},
    }


def _town_buildings(lay: Layout, rng: random.Random, table: AssetTable) -> int:
    """도로변 줄 세우기 + 뒷골목 격자. 완전 랜덤이면 마을이 아니라 숲이 된다."""
    mid = TOWN_MID
    wall_min, wall_max = TOWN_WALL_MIN, TOWN_WALL_MAX
    plaza_r = PLAZA_HALF
    placed = 0

    # 1) 대로변 — 도로를 향하게 세운다.
    street_mix = [
        "bld_house_a", "bld_house_a", "bld_house_a", "bld_barracks",
        "bld_house_a", "bld_archery_range", "bld_house_a", "bld_lumbermill",
    ]
    frontage = 6.0 + 9.0
    pitch = 13.0

    for axis in ("x", "z"):
        t = wall_min + 14.0
        i = 0
        while t < wall_max - 14.0:
            for side in (-1, 1):
                if abs(t - mid) < plaza_r + 12.0:
                    continue
                asset = street_mix[(i + (0 if side < 0 else 4)) % len(street_mix)]
                if axis == "x":
                    x, z = t, mid + side * frontage
                    yaw = math.pi if side < 0 else 0.0
                else:
                    x, z = mid + side * frontage, t
                    yaw = -math.pi / 2 if side < 0 else math.pi / 2
                if lay.try_place(asset, x, z, yaw, max_slope=0.30):
                    placed += 1
            t += pitch
            i += 1

    # 2) 두 번째 줄 — 대로에서 한 겹 뒤.
    for axis in ("x", "z"):
        t = wall_min + 20.0
        i = 0
        while t < wall_max - 20.0:
            for side in (-1, 1):
                if abs(t - mid) < plaza_r + 14.0:
                    continue
                asset = street_mix[(i * 3 + (1 if side < 0 else 5)) % len(street_mix)]
                if axis == "x":
                    x, z = t, mid + side * (frontage + 20.0)
                    yaw = math.pi if side < 0 else 0.0
                else:
                    x, z = mid + side * (frontage + 20.0), t
                    yaw = -math.pi / 2 if side < 0 else math.pi / 2
                if lay.try_place(asset, x, z, yaw, max_slope=0.30):
                    placed += 1
            t += pitch + 2.0
            i += 1

    # 3) 광장 둘레 — 공공 건물이 광장을 향해 둘러선다.
    civic = ["bld_barracks", "bld_mill", "bld_archery_range", "bld_lumbermill",
             "bld_house_a", "bld_mine"]
    ring = plaza_r + 18.0
    for i in range(10):
        a = math.tau * i / 10 + 0.31
        x, z = mid + math.cos(a) * ring, mid + math.sin(a) * ring
        if lay.try_place(civic[i % len(civic)], x, z, a + math.pi / 2, max_slope=0.35):
            placed += 1

    # 4) 뒷골목 — 느슨한 격자로 빈 구획을 메운다.
    # bld_watermill 은 여기서 빠졌다 — 개울가에만 세운다(물 없는 물레방아 금지).
    block_mix = ["bld_house_a"] * 6 + ["bld_mill", "bld_house_a", "bld_barracks", "bld_mine"]
    for gx in range(9):
        for gz in range(9):
            x = wall_min + 14.0 + gx * 20.5
            z = wall_min + 14.0 + gz * 20.5
            x += rng.uniform(-3.0, 3.0)
            z += rng.uniform(-3.0, 3.0)
            asset = block_mix[(gx * 7 + gz * 5) % len(block_mix)]
            yaw = rng.choice([0.0, math.pi / 2, math.pi, -math.pi / 2])
            if lay.try_place(asset, x, z, yaw, max_slope=0.30):
                placed += 1

    return placed


def _town_props(lay: Layout, rng: random.Random, table: AssetTable) -> None:
    """가로등, 통, 상자, 울타리, 노점. 마을이 '사람이 사는 곳'으로 보이게 한다."""
    mid = TOWN_MID
    wall_min, wall_max = TOWN_WALL_MIN, TOWN_WALL_MAX
    plaza_r = PLAZA_HALF

    # 대로변 가로등 — 양쪽에 규칙적으로.
    for axis in ("x", "z"):
        t = wall_min + 10.0
        while t < wall_max - 10.0:
            for side in (-1, 1):
                if abs(t - mid) < plaza_r + 3.0:
                    continue
                x, z = (t, mid + side * 8.0) if axis == "x" else (mid + side * 8.0, t)
                lay.try_place("prop_lamp_post", x, z, 0.0)
            t += 26.0

    # 광장은 사각이므로 소품도 변을 따라 배치한다.
    #
    # 여기서 try_place 를 쓰면 전부 거절된다. 광장 전체가 이미 reserve 로
    # 막혀 있기 때문이다(식생이 광장에 올라오지 못하게 하려고). 의도한 배치는
    # 겹침 판정을 거치지 않는 place 로 놓는다.
    plaza_y = lay.height.sample(mid, mid)
    road_gap = 8.0   # 대로가 지나가는 구간은 비운다

    # (변 방향 단위벡터, 바깥 방향 단위벡터)
    sides = [
        ((1.0, 0.0), (0.0, -1.0)),   # 북변
        ((1.0, 0.0), (0.0, 1.0)),    # 남변
        ((0.0, 1.0), (-1.0, 0.0)),   # 서변
        ((0.0, 1.0), (1.0, 0.0)),    # 동변
    ]

    # 노점 — 변 안쪽으로 5m 들어온 자리에 늘어선다.
    #
    # 회전 주의: 모델 로컬 +Z 는 yaw 회전 뒤 월드 (sin yaw, cos yaw) 를 향한다.
    # 탁자는 Z축이 긴 쪽(2 x 4m)이므로 변 방향 (ux, uz) 에 맞추려면
    # yaw = atan2(ux, uz) 다. 이전에 atan2(-ox, -oz) 를 써서 90도 틀어져 있었다.
    for (ux, uz), (ox, oz) in sides:
        table_yaw = math.atan2(ux, uz)
        for t in (-16.0, -9.0, 9.0, 16.0):
            bx = mid + ox * (PLAZA_HALF - 5.0) + ux * t
            bz = mid + oz * (PLAZA_HALF - 5.0) + uz * t
            lay.place("prop_table_long", bx, bz, table_yaw)
            lay.place("prop_stool", bx - ox * 2.2, bz - oz * 2.2,
                      rng.uniform(0, math.tau))
            lay.place(
                rng.choice(["prop_barrel_small", "prop_crates", "prop_barrel_stack", "prop_keg"]),
                bx + ux * 3.0, bz + uz * 3.0, rng.uniform(0, math.tau))

    # 난간 + 가로등 — 대지 가장자리를 따라. 계단 앞 통행로는 비운다.
    #
    # 난간은 X축이 긴 쪽(4 x 0.5m)이다. 로컬 +X 는 yaw 회전 뒤
    # (cos yaw, -sin yaw) 를 향하므로 변 방향에 맞추려면 yaw = atan2(-uz, ux) 다.
    # atan2(ux, uz) 를 쓰는 바람에 90도 틀어져 빗살처럼 꽂혀 있었다.
    seg = lay.table.width_x("prop_fence")            # 4.0 m
    edge = PLAZA_HALF - 0.6
    for (ux, uz), (ox, oz) in sides:
        fence_yaw = math.atan2(-uz, ux)
        t = -PLAZA_HALF + seg / 2
        while t < PLAZA_HALF:
            if abs(t) > road_gap:
                fx = mid + ox * edge + ux * t
                fz = mid + oz * edge + uz * t
                lay.place("prop_fence", fx, fz, fence_yaw, radius=seg * 0.4)
            t += seg
        # 통행로 양옆에 가로등
        for k in (-1.0, 1.0):
            lay.place("prop_lamp_post",
                      mid + ox * (edge - 1.5) + ux * (road_gap + 1.5) * k,
                      mid + oz * (edge - 1.5) + uz * (road_gap + 1.5) * k,
                      0.0)

    # 우물 주변 화단.
    # 반경 4.5 에 원본 크기로 두면 꽃(2.05m)이 우물보다 커서 우물을 가린다.
    # 밖으로 빼고 배율을 낮춘다.
    for i in range(8):
        a = math.tau * i / 8 + 0.4
        lay.place(rng.choice(["prop_flower_group_01", "prop_flower_group_02"]),
                  mid + math.cos(a) * 7.5, mid + math.sin(a) * 7.5,
                  rng.uniform(0, math.tau), scale=0.7)

    # 골목 소품 — 건물 사이에 흩어놓는다.
    clutter = ["prop_barrel_large", "prop_barrel_small", "prop_barrel_stack",
               "prop_crates", "prop_box_small", "prop_keg", "prop_chair"]
    scatter(lay, rng, clutter, 180, wall_min + 8, wall_max - 8, wall_min + 8, wall_max - 8,
            max_slope=0.25)

    # 텃밭 울타리 — 4m 모듈을 짧게 이어 마당 경계를 만든다.
    fence_len = lay.table.width_x("prop_fence")
    for _ in range(26):
        x = rng.uniform(wall_min + 16, wall_max - 16)
        z = rng.uniform(wall_min + 16, wall_max - 16)
        horizontal = rng.random() < 0.5
        yaw = 0.0 if horizontal else math.pi / 2
        run = rng.randint(2, 4)
        for k in range(run):
            ox = fence_len * k if horizontal else 0.0
            oz = 0.0 if horizontal else fence_len * k
            lay.try_place("prop_fence", x + ox, z + oz, yaw, max_slope=0.22)


def _mark_smoke(lay: Layout, rng: random.Random) -> int:
    """어느 집에서 연기가 나는지 **배치 데이터에** 적는다.

    manifest 의 chimney 는 "그 모델 어디에 굴뚝이 있나"이고(모델의 성질),
    여기서 정하는 것은 "그 집에 지금 불이 지펴져 있나"다(배치의 성질).
    둘은 다른 층위라 파일도 다르다.

    **절반만** 켠다. 42채 전부에서 연기가 나면 오히려 균일해 보인다.
    seed 를 고정했으므로 같은 집에서 항상 난다.
    """
    # 성은 뺀다. 검출된 지점(y=14.5m)이 굴뚝인지 첨탑 끝인지 확실하지 않고,
    # 랜드마크라 틀렸을 때 제일 눈에 띈다. 집만 켠다.
    eligible = [p for p in lay.placements if p["asset"] == "bld_house_a"]
    lit = 0
    for item in eligible:
        if rng.random() < 0.5:
            item["smoke"] = True
            lit += 1
    print(f"  굴뚝 연기: {lit} / {len(eligible)} 채")
    return lit


def _town_npcs(lay: Layout, market_x: float, market_z: float,
               mid: float, wall_max: float) -> list[dict]:
    """NPC 도 데이터다. 서버가 같은 좌표를 읽어 상호작용 판정을 한다."""
    # 성 계단 윗단. 위병은 여기서 올라오는 쪽(남쪽)을 본다.
    # yaw = 0 은 모델이 -Z(북)를 보는 것이라 계단에 등을 돌린다 — 이전 버그.
    mound_top_edge = CASTLE_MOUND.z + CASTLE_MOUND.radius - 3.0

    defs: list[tuple[str, str, float, float, float, str]] = [
        # (id, asset, x, z, yaw, role)
        ("npc_merchant_01", "chr_npc_merchant", market_x + 7.0, market_z + 6.0, -2.4, "merchant"),
        # 남쪽 성문 위병 — 문 안쪽에서 바깥을 본다.
        ("npc_guard_gate_s1", "chr_npc_guard", mid - 8.0, wall_max - 6.0, math.pi, "guard"),
        ("npc_guard_gate_s2", "chr_npc_guard", mid + 8.0, wall_max - 6.0, math.pi, "guard"),
        # 성 계단 위병 — 계단 양옆, 오르는 사람을 마주본다.
        ("npc_guard_castle_1", "chr_npc_guard", CASTLE_MOUND.x - 8.0, mound_top_edge, math.pi, "guard"),
        ("npc_guard_castle_2", "chr_npc_guard", CASTLE_MOUND.x + 8.0, mound_top_edge, math.pi, "guard"),
    ]

    # 주민은 광장 노점 앞에 세운다.
    # 전에는 대로를 따라 mid±52~58 에 흩어 놨는데, 북쪽 하나가 성 언덕 위로
    # 올라가 버려서 마을 사람이 성 꼭대기에 혼자 서 있었다.
    stall_sides = [
        ((1.0, 0.0), (0.0, -1.0)),   # 북변
        ((1.0, 0.0), (0.0, 1.0)),    # 남변
        ((0.0, 1.0), (-1.0, 0.0)),   # 서변
        ((0.0, 1.0), (1.0, 0.0)),    # 동변
    ]
    villager_assets = [
        "chr_npc_villager_a", "chr_npc_villager_b",
        "chr_npc_villager_c", "chr_npc_villager_d",
    ]
    n = 0
    for (ux, uz), (ox, oz) in stall_sides:
        for t in (-9.0, 9.0):
            # 노점(변 안쪽 5m)에서 광장 중심 쪽으로 3m 떨어져 선다.
            x = mid + ox * (PLAZA_HALF - 8.0) + ux * t
            z = mid + oz * (PLAZA_HALF - 8.0) + uz * t
            # 노점을 바라본다 = 바깥 방향. 모델 앞면이 -Z 이므로 atan2(ox, oz).
            defs.append((
                f"npc_villager_{n + 1:02d}",
                villager_assets[n % len(villager_assets)],
                x, z, math.atan2(ox, oz), "villager",
            ))
            n += 1

    npcs: list[dict] = []
    for npc_id, asset, x, z, yaw, role in defs:
        y = lay.height.sample(x, z)
        clip, variants = NPC_IDLE.get(role, NPC_IDLE["villager"])
        npcs.append({
            "id": npc_id,
            "asset": asset,
            "pos": [round(x, 3), round(y, 3), round(z, 3)],
            "yaw": round(yaw, 4),
            "role": role,
            "animation": clip,
            "idle_variants": list(variants),
        })
        lay.reserve(x, z, CLEAR_RADIUS)
    return npcs


# ----------------------------------------------------------------------------
# 필드 (512 x 512) — 전투지역
# ----------------------------------------------------------------------------


def build_field(table: AssetTable, rng: random.Random) -> dict:
    size = FIELD_SIZE

    height = build_heightmap(
        size,
        8.0,
        rng,
        bumps=[
            Bump(120.0, 120.0, 150.0, 22.0),
            Bump(390.0, 100.0, 130.0, 18.0),
            Bump(420.0, 380.0, 160.0, 26.0),
            Bump(90.0, 400.0, 140.0, 16.0),
            Bump(256.0, 256.0, 110.0, -9.0),   # 가운데는 얕은 분지
        ],
        plateaus=[
            # 스폰 구역은 평평해야 몬스터가 경사에 끼지 않는다.
            Plateau(150.0, 180.0, 30.0, 22.0, 6.0),
            Plateau(360.0, 150.0, 26.0, 20.0, 9.0),
            Plateau(256.0, 300.0, 22.0, 20.0, 2.0),
            Plateau(256.0, 430.0, 46.0, 26.0, 12.0),
            Plateau(256.0, 24.0, 20.0, 24.0, 0.0),  # 마을에서 들어오는 입구
        ],
        noise_amplitude=2.4,
        noise_scale=64.0,
    )

    # 마을과 같은 이유 — 걸어 오를 수 없는 급경사를 남기지 않는다.
    before = height.max_slope()
    height.limit_slope(MAX_WALK_SLOPE)
    after = height.max_slope()
    print(f"  경사 정리: 최대 tan {before:.2f}({math.degrees(math.atan(before)):.0f}도)"
          f" -> {after:.2f}({math.degrees(math.atan(after)):.0f}도)   걸을 수 있는 한계 52도")

    # --- 강과 호수 --------------------------------------------------------------
    # 마을 개울과 같은 이유로 경사 정리 뒤에 판다.
    river, _ = _stream_profile(height, FIELD_RIVER, sink=1.0, grade=0.010, drop=FIELD_RAPIDS)
    height.carve_channel(river, 7.0, 1.8, 9.0)

    # 호수 — 강의 종착점. 분지 바닥이라 물이 고이는 것이 자연스럽다.
    # 한 점짜리 꺾은선은 만들 수 없으므로 아주 짧은 구간을 넓게 판다.
    lake_x, lake_z, lake_r = FIELD_LAKE
    lake_y = river[-1][2]
    height.carve_channel(
        [(lake_x, lake_z - 0.5, lake_y), (lake_x, lake_z + 0.5, lake_y)],
        lake_r, 3.0, 12.0)

    water_points = height.finish_water()
    print(f"  강: 수면 {river[0][2]:.1f}m -> {river[-1][2]:.1f}m,"
          f" 급류 {FIELD_RAPIDS[1]:.1f}m, 호수 반경 {lake_r:.0f}m,"
          f" 물 격자점 {water_points}, 수면 구멍 {height.water_holes()}")

    lay = Layout(table, height)

    # 물 위에는 아무것도 놓지 않는다.
    _reserve_along(lay, river, 7.0 + 4.5)
    lay.reserve(lake_x, lake_z, lake_r + 6.0)

    entry = [256.0, round(height.sample(256.0, 24.0), 3), 24.0]
    lay.reserve(entry[0], entry[2], 12.0)

    spawns = [
        {
            "id": "spawn_warrior_a", "monster": "skel_warrior_01",
            "asset": "chr_skeleton_warrior",
            "center": [150.0, round(height.sample(150.0, 180.0), 3), 180.0],
            "radius": 28.0, "count": 8, "respawn_ms": 30000,
        },
        {
            "id": "spawn_warrior_b", "monster": "skel_warrior_01",
            "asset": "chr_skeleton_warrior",
            "center": [360.0, round(height.sample(360.0, 150.0), 3), 150.0],
            "radius": 24.0, "count": 6, "respawn_ms": 30000,
        },
        {
            "id": "spawn_minion_pack", "monster": "skel_minion_01",
            "asset": "chr_skeleton_minion",
            "center": [256.0, round(height.sample(256.0, 300.0), 3), 300.0],
            "radius": 18.0, "count": 12, "respawn_ms": 20000,
            "note": "어그로 전파 검증용. 한 마리 피격 시 반경 내 동족이 참전한다.",
        },
        {
            "id": "spawn_elite", "monster": "skel_elite_01",
            "asset": "chr_skeleton_elite",
            "center": [256.0, round(height.sample(256.0, 430.0), 3), 430.0],
            "radius": 10.0, "count": 1, "respawn_ms": 120000,
            "patrol_route": "patrol_elite_01",
        },
    ]

    waypoints = [(214.0, 404.0), (298.0, 404.0), (298.0, 456.0), (214.0, 456.0)]
    patrol_routes = [{
        "id": "patrol_elite_01",
        "loop": True,
        "waypoints": [[x, round(height.sample(x, z), 3), z] for x, z in waypoints],
    }]

    for sp in spawns:
        lay.reserve(sp["center"][0], sp["center"][2], sp["radius"] + CLEAR_RADIUS)
    for route in patrol_routes:
        pts = route["waypoints"]
        for i in range(len(pts)):
            ax, _, az = pts[i]
            bx, _, bz = pts[(i + 1) % len(pts)]
            length = math.hypot(bx - ax, bz - az)
            n = max(1, int(length / 4.0))
            for s in range(n + 1):
                t = s / n
                lay.reserve(ax + (bx - ax) * t, az + (bz - az) * t, 4.0)

    forest = [
        "prop_tree_pine_01", "prop_tree_pine_02", "prop_tree_pine_03", "prop_tree_pine_04",
        "prop_tree_common_01", "prop_tree_common_02", "prop_tree_common_03", "prop_tree_common_04",
    ]
    dead = ["prop_tree_dead_01", "prop_tree_dead_02", "prop_tree_dead_03"]
    rocks = ["prop_rock_medium_01", "prop_rock_medium_02", "prop_rock_medium_03", "prop_med_rocks"]
    small = [
        "prop_bush_common_01", "prop_grass_tall_01", "prop_grass_wispy_01",
        "prop_pebble_round_01", "prop_pebble_round_02", "prop_flower_group_01",
        "prop_clover_01", "prop_mushroom_01",
    ]

    scatter(lay, rng, forest, 760, 6, size - 6, 6, size - 6, max_slope=0.55)
    scatter(lay, rng, dead, 110, 180, 340, 260, 470, max_slope=0.55)
    scatter(lay, rng, rocks, 190, 6, size - 6, 6, size - 6)
    scatter(lay, rng, small, 620, 6, size - 6, 6, size - 6)

    return {
        "schema_version": 2,
        "id": "field_01",
        "display_name": "필드",
        "generated": date.today().isoformat(),
        "generator": "tools/gen_layout.py",
        "seed": SEED,
        "origin": [0.0, 0.0],
        "size": [size, size],
        "y_range": [0, 64],
        "sector_size": 32,
        "terrain": height.to_json(),
        "ground": {"color": "#4c6b37", "nav": "include"},
        # 마을과 같은 규약 — 물은 내비메시에서 빠진다.
        "water": {
            "color": "#3d7a92", "deep_color": "#15414f", "foam_color": "#dff1f5",
            "nav": "exclude",
            "cells": height.water_cells(),
        },
        "flags": {"pvp": True, "spawn": True},
        "spawn_point": entry,
        "npcs": [],
        "spawns": spawns,
        "patrol_routes": patrol_routes,
        "placements": lay.placements,
        "stats": {},
    }


# ----------------------------------------------------------------------------


def scatter(
    lay: Layout,
    rng: random.Random,
    assets: list[str],
    target: int,
    x0: float,
    x1: float,
    z0: float,
    z1: float,
    *,
    exclude_box: tuple[float, float, float, float] | None = None,
    max_slope: float | None = None,
    max_tries_per_item: int = 24,
) -> int:
    """거절 표본으로 흩뿌린다. 겹치면 버리고 다시 뽑는다.

    반경은 실측값이라 큰 나무일수록 자연히 넓게 떨어진다.
    target 은 '시도할 개수'이고 실제 배치 수는 그보다 적다 — 밀도가
    포화하면 더 놓을 자리가 없기 때문이다. 반환값이 실제 배치 수다.
    """
    placed = 0
    for _ in range(target):
        asset = rng.choice(assets)
        for _ in range(max_tries_per_item):
            x = rng.uniform(x0, x1)
            z = rng.uniform(z0, z1)
            if exclude_box is not None:
                ex0, ex1, ez0, ez1 = exclude_box
                if ex0 <= x <= ex1 and ez0 <= z <= ez1:
                    continue
            if lay.try_place(asset, x, z, rng.uniform(0.0, math.tau), max_slope=max_slope):
                placed += 1
                break
    return placed


def write_region(region: dict) -> Path:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / f"{region['id']}.json"
    path.write_text(
        json.dumps(region, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return path


def main(argv: list[str]) -> int:
    table = AssetTable(REPO)
    wanted = set(argv[1:]) or {"town", "field_01"}

    builders = {"town": build_town, "field_01": build_field}
    unknown = wanted - builders.keys()
    if unknown:
        print(f"알 수 없는 리전: {', '.join(sorted(unknown))}", file=sys.stderr)
        return 1

    for name in ("town", "field_01"):
        if name not in wanted:
            continue
        # 리전마다 seed 를 분리해 한쪽을 고쳐도 다른 쪽 배치가 흔들리지 않게 한다.
        rng = random.Random(f"{SEED}:{name}")
        region = builders[name](table, rng)
        path = write_region(region)

        counts: dict[str, int] = {}
        for p in region["placements"]:
            counts[p["asset"]] = counts.get(p["asset"], 0) + 1
        monsters = sum(s["count"] for s in region["spawns"])
        hs = region["terrain"]["heights"]
        print(
            f"{name:<10} 배치 {len(region['placements']):>5}  종류 {len(counts):>2}  "
            f"NPC {len(region['npcs']):>2}  몬스터 {monsters:>3}  "
            f"지형 {region['terrain']['resolution']}²  "
            f"높이 {min(hs):.1f}~{max(hs):.1f}m  -> {path.relative_to(REPO)}"
        )

    # JSON 을 새로 썼으면 .bin 도 **바로** 다시 만든다.
    #
    # 따로 돌리게 두면 반드시 잊는다. 그러면 서버는 옛 지형을, 클라는 새 지형을
    # 읽는 상태가 되는데 그건 5장이 통째로 막으려는 바로 그 어긋남이다.
    import build_region

    built = sorted(n for n in ("town", "field_01") if n in wanted)
    for name in built:
        src = build_region.SRC_DIR / f"{name}.json"
        out = build_region.OUT_DIR / f"{name}.bin"
        build_region.write(out, build_region.compile_region(src))
        build_region.verify(out, src)
    print(f"{'바이너리':<10} 갱신 {', '.join(built)}  (tools/build_region.py 와 같은 산출물)")

    # 콜리전 메시(Recast 입력)도 같이 만든다. 지형이 바뀌면 내비메시도 낡는다.
    import build_collision

    for name in built:
        build_collision.build(name)
    print(f"{'내비메시':<10} 다시 구우려면: Binary/Debug/GameServer.exe --build-navmesh")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
