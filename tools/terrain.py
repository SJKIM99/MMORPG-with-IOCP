"""높이맵 생성과 샘플링.

CLAUDE.md 5장의 연장이다 — 지형도 배치와 같은 JSON 안에 들어간다.
서버(Recast 입력 메시)와 클라(지면 메시)가 같은 배열을 읽어야
"클라에선 언덕, 서버에선 평지" 같은 어긋남이 생기지 않는다.

**생성 함수가 아니라 샘플링된 배열을 저장한다.** 함수를 저장하고 양쪽에서
다시 계산하게 하면 Python / C# / C++ 의 부동소수점 차이로 갈라질 수 있다.
배열은 갈라질 여지가 없다.

격자는 (n+1) x (n+1) 개의 꼭짓점이고, n = size / cell 이다.
인덱스는 row-major — index = iz * (n+1) + ix.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass, field


# 물이 없는 격자점의 수면 높이. 실제 지형은 y_range 0~64 이므로 이 값과
# 겹칠 일이 없다. JSON 에 NaN 을 넣을 수 없어(표준 JSON 이 아니다) 센티널을 쓴다.
NO_WATER = -1000.0


def _smoothstep(edge0: float, edge1: float, x: float) -> float:
    if edge1 <= edge0:
        return 0.0 if x < edge0 else 1.0
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


@dataclass
class Heightmap:
    size: float       # 리전 한 변의 길이 (m)
    cell: float       # 격자 간격 (m)
    heights: list[float]

    # 포장 여부. 0 = 풀, 1 = 포석. heights 와 같은 배열 구조다.
    #
    # 도로를 **타일 모델로 깔지 않고 지면에 칠한다.** 평평한 타일을 경사에
    # 놓으면 낮은 쪽 모서리가 뜨고(틈), 타일마다 턱이 생겨 캐릭터가 순간이동한다.
    # 지면 자체를 칠하면 그런 문제가 원천적으로 없다.
    surface: list[float] = field(default_factory=list)

    # 수면 높이. NO_WATER 면 그 격자점에는 물이 없다.
    #
    # 지형 높이와 따로 두는 이유: 물은 **수평면**이고 지형은 아니다. 하나의
    # 배열로 합치면 개울 바닥이 곧 수면이 되어 물이 지형을 따라 출렁인다.
    # 클라이언트는 이 배열만으로 수면 메시를 만든다(WaterMesh).
    water: list[float] = field(default_factory=list)

    def __post_init__(self) -> None:
        if not self.surface:
            self.surface = [0.0] * len(self.heights)
        if not self.water:
            self.water = [NO_WATER] * len(self.heights)

    @property
    def n(self) -> int:
        """한 변의 칸 수. 꼭짓점은 n+1 개."""
        return int(round(self.size / self.cell))

    def index(self, ix: int, iz: int) -> int:
        return iz * (self.n + 1) + ix

    def at(self, ix: int, iz: int) -> float:
        ix = max(0, min(self.n, ix))
        iz = max(0, min(self.n, iz))
        return self.heights[self.index(ix, iz)]

    def sample(self, x: float, z: float) -> float:
        """쌍선형 보간. 클라이언트의 지면 메시와 같은 면을 돌려준다."""
        fx = max(0.0, min(self.size, x)) / self.cell
        fz = max(0.0, min(self.size, z)) / self.cell
        ix, iz = int(fx), int(fz)
        tx, tz = fx - ix, fz - iz

        h00 = self.at(ix, iz)
        h10 = self.at(ix + 1, iz)
        h01 = self.at(ix, iz + 1)
        h11 = self.at(ix + 1, iz + 1)

        return (
            h00 * (1 - tx) * (1 - tz)
            + h10 * tx * (1 - tz)
            + h01 * (1 - tx) * tz
            + h11 * tx * tz
        )

    def footprint_min(self, x: float, z: float, radius: float) -> float:
        """반경 안에서 가장 낮은 높이.

        경사면에 건물을 놓을 때 쓴다. 중심 높이에 맞추면 낮은 쪽 모서리가
        공중에 뜬다. 가장 낮은 지점에 맞추면 높은 쪽이 지형에 살짝 묻히는데,
        그쪽이 훨씬 자연스럽다.
        """
        lowest = self.sample(x, z)
        for i in range(8):
            a = math.tau * i / 8
            lowest = min(
                lowest,
                self.sample(x + math.cos(a) * radius, z + math.sin(a) * radius),
            )
        return lowest

    def footprint_max(self, x: float, z: float, radius: float) -> float:
        """반경 안에서 가장 높은 높이.

        포석처럼 얇고 평평한 타일에 쓴다. 최저점에 맞추면 경사면에서 오르막 쪽
        모서리가 지형에 묻혀 도로가 얼룩말 무늬가 된다. 최고점에 맞추면 반대로
        내리막 쪽에 몇 cm 턱이 생기는데, 포석 단차로 보여 훨씬 낫다.
        """
        highest = self.sample(x, z)
        for i in range(8):
            a = math.tau * i / 8
            highest = max(
                highest,
                self.sample(x + math.cos(a) * radius, z + math.sin(a) * radius),
            )
        return highest

    def slope_at(self, x: float, z: float) -> float:
        """경사도(0 = 평지, 1 = 45도). 급경사에 건물을 세우지 않으려고 본다."""
        d = self.cell
        hx = self.sample(x + d, z) - self.sample(x - d, z)
        hz = self.sample(x, z + d) - self.sample(x, z - d)
        return math.hypot(hx, hz) / (2.0 * d)

    def grade_corridor(
        self,
        x0: float,
        z0: float,
        x1: float,
        z1: float,
        half_width: float,
        feather: float = 6.0,
    ) -> None:
        """도로 통로를 중심선 높이로 평탄화한다 (폭 방향으로).

        경사면에 평평한 타일을 깔면 타일마다 지형에 물려 경계가 검은 줄로 보인다.
        실제 도로도 이렇게 노반을 깎아 만든다. 세로 방향 기울기는 남기고
        가로 방향만 수평으로 만든다.
        """
        snapshot = list(self.heights)

        def sample_from(src: list[float], x: float, z: float) -> float:
            fx = max(0.0, min(self.size, x)) / self.cell
            fz = max(0.0, min(self.size, z)) / self.cell
            ix, iz = int(fx), int(fz)
            tx, tz = fx - ix, fz - iz

            def at(a: int, b: int) -> float:
                a = max(0, min(self.n, a))
                b = max(0, min(self.n, b))
                return src[b * (self.n + 1) + a]

            return (
                at(ix, iz) * (1 - tx) * (1 - tz)
                + at(ix + 1, iz) * tx * (1 - tz)
                + at(ix, iz + 1) * (1 - tx) * tz
                + at(ix + 1, iz + 1) * tx * tz
            )

        dx, dz = x1 - x0, z1 - z0
        seg_len2 = dx * dx + dz * dz
        if seg_len2 <= 0.0:
            return

        for iz in range(self.n + 1):
            z = iz * self.cell
            for ix in range(self.n + 1):
                x = ix * self.cell

                # 선분 위로 투영
                t = ((x - x0) * dx + (z - z0) * dz) / seg_len2
                t = max(0.0, min(1.0, t))
                px, pz = x0 + dx * t, z0 + dz * t
                dist = math.hypot(x - px, z - pz)
                if dist > half_width + feather:
                    continue

                w = 1.0 - _smoothstep(half_width, half_width + feather, dist)
                center_h = sample_from(snapshot, px, pz)
                i = self.index(ix, iz)
                self.heights[i] = self.heights[i] * (1.0 - w) + center_h * w

    def grade_flat(
        self,
        x: float,
        z: float,
        half_x: float,
        half_z: float,
        y: float,
        feather: float = 4.0,
    ) -> None:
        """사각 영역을 지정한 높이로 눌러 평탄하게 만든다.

        계단 발치를 만드는 데 쓴다. 계단은 평면 모델이라 경사 위에 놓으면
        한쪽이 뜨고 반대쪽이 묻힌다. 밑단이 놓일 자리를 먼저 깎아야 한다.
        """
        for iz in range(self.n + 1):
            gz = iz * self.cell
            for ix in range(self.n + 1):
                gx = ix * self.cell
                dx = max(0.0, abs(gx - x) - half_x)
                dz = max(0.0, abs(gz - z) - half_z)
                dist = math.hypot(dx, dz)
                if dist > feather:
                    continue

                # feather 가 0 이면 _smoothstep(0, 0, 0) 이 1 을 돌려주어
                # 가중치가 0 이 된다 — 안쪽까지 하나도 평탄해지지 않는다.
                # (광장 상판 재평탄화가 통째로 무효였던 원인이다.)
                if feather <= 0.0:
                    w = 1.0 if dist <= 0.0 else 0.0
                else:
                    w = 1.0 - _smoothstep(0.0, feather, dist)

                i = self.index(ix, iz)
                self.heights[i] = self.heights[i] * (1.0 - w) + y * w

    def grade_ramp(
        self,
        x0: float,
        z0: float,
        y0: float,
        x1: float,
        z1: float,
        y1: float,
        half_width: float,
        feather: float = 3.0,
    ) -> None:
        """두 점 사이를 직선 경사로 깎는다.

        계단은 직선이고 대지 치맛자락은 S 자(smoothstep)라 그냥 얹으면 중간이
        어긋난다(실측 1.34m). 계단이 놓일 띠만 직선으로 깎아 맞춘다.
        선분 밖으로는 양 끝 높이가 그대로 이어진다.
        """
        dx, dz = x1 - x0, z1 - z0
        seg_len2 = dx * dx + dz * dz
        if seg_len2 <= 0.0:
            return

        for iz in range(self.n + 1):
            gz = iz * self.cell
            for ix in range(self.n + 1):
                gx = ix * self.cell
                tt = ((gx - x0) * dx + (gz - z0) * dz) / seg_len2
                tt = max(0.0, min(1.0, tt))
                px, pz = x0 + dx * tt, z0 + dz * tt
                dist = math.hypot(gx - px, gz - pz)
                if dist > half_width + feather:
                    continue
                if dist <= half_width:
                    w = 1.0
                elif feather <= 0.0:
                    continue
                else:
                    w = 1.0 - _smoothstep(half_width, half_width + feather, dist)
                i = self.index(ix, iz)
                self.heights[i] = self.heights[i] * (1.0 - w) + (y0 + (y1 - y0) * tt) * w

    def grade_ramp_span(
        self,
        x0: float,
        z0: float,
        y0: float,
        x1: float,
        z1: float,
        y1: float,
        half_width: float,
        feather: float = 3.0,
        feather_along: float | None = None,
    ) -> None:
        """grade_ramp 와 같되 **선분 끝에서 끊긴다.**

        grade_ramp 는 점과 선분의 거리로 판정하므로 모양이 캡슐이다 — 양 끝에서
        half_width 만큼 더 번져 나간다. 계단에서는 그게 이득이었지만(끝을
        주변에 이어 붙여 준다) 도로를 깎을 때는 재앙이다.

        실제로 다리 둑을 grade_ramp 로 돋웠더니 끝의 둥근 캡이 개울 한복판까지
        뻗어 **폭 12m 짜리 물길을 통째로 메웠다.** 여기서는 진행 방향으로도
        경계를 둔다.
        """
        dx, dz = x1 - x0, z1 - z0
        seg_len = math.hypot(dx, dz)
        if seg_len <= 0.0:
            return
        ux, uz = dx / seg_len, dz / seg_len
        # 진행 방향 feather 는 따로 줄 수 있다. 다리 둑처럼 "여기서 딱 끝나야
        # 하는" 경우에는 옆으로는 부드럽게 퍼지되 앞뒤로는 바짝 끊어야 한다.
        along_feather = feather if feather_along is None else feather_along

        for iz in range(self.n + 1):
            gz = iz * self.cell
            for ix in range(self.n + 1):
                gx = ix * self.cell

                # 진행 방향 거리(0..seg_len)와 직각 거리로 나눠 본다.
                along = (gx - x0) * ux + (gz - z0) * uz
                across = abs(-(gx - x0) * uz + (gz - z0) * ux)

                if across > half_width + feather:
                    continue
                # 선분 밖은 feather 안에서만 살짝 이어 붙이고 그 너머는 건드리지 않는다.
                if along < -along_feather or along > seg_len + along_feather:
                    continue

                t = max(0.0, min(1.0, along / seg_len))
                y = y0 + (y1 - y0) * t

                w_across = (1.0 if across <= half_width
                            else 1.0 - _smoothstep(half_width, half_width + feather, across))
                over = 0.0 if 0.0 <= along <= seg_len else min(-along, along - seg_len)
                w_along = (1.0 if over <= 0.0 or along_feather <= 0.0
                           else 1.0 - _smoothstep(0.0, along_feather, over))

                w = w_across * w_along
                i = self.index(ix, iz)
                self.heights[i] = self.heights[i] * (1.0 - w) + y * w

    def grade_disc(
        self,
        x: float,
        z: float,
        radius: float,
        y: float,
        feather: float = 0.0,
    ) -> None:
        """원형 영역을 지정한 높이로 눌러 평탄하게 만든다.

        원형 대지(Plateau)의 상판을 다시 고정할 때 쓴다. 계단 발치를 깎는
        grade_flat 이 대지 안쪽까지 파고들면 계단이 붙을 경사면이 사라진다.
        """
        for iz in range(self.n + 1):
            gz = iz * self.cell
            for ix in range(self.n + 1):
                gx = ix * self.cell
                dist = math.hypot(gx - x, gz - z)
                if dist > radius + feather:
                    continue
                if dist <= radius:
                    w = 1.0
                elif feather <= 0.0:
                    continue
                else:
                    w = 1.0 - _smoothstep(radius, radius + feather, dist)
                i = self.index(ix, iz)
                self.heights[i] = self.heights[i] * (1.0 - w) + y * w

    # --- 경사 제한 -----------------------------------------------------------

    def max_slope(self) -> float:
        """인접 격자점 사이 최대 기울기(tan). 걸을 수 있는지 판단하는 기준이다."""
        worst = 0.0
        for iz in range(self.n + 1):
            for ix in range(self.n + 1):
                here = self.at(ix, iz)
                for dx, dz in ((1, 0), (0, 1)):
                    if ix + dx > self.n or iz + dz > self.n:
                        continue
                    worst = max(worst, abs(self.at(ix + dx, iz + dz) - here) / self.cell)
        return worst

    def steep_cells(self, max_ratio: float) -> list[tuple[float, float, float]]:
        """인접 격자점 사이 기울기가 한계를 넘는 곳을 모두 찾는다.

        반환: (x, z, 기울기) 목록. 기울기는 tan 값이다.

        Godot 의 MoveAndSlide 는 법선이 FloorMaxAngle 을 넘으면 그 면을
        **바닥이 아니라 벽**으로 분류한다. 그러면 걸어 오를 수 없고,
        step-up 로직이 개입해 캐릭터를 한 번에 들어 올린다 — 순간이동이다.
        """
        found: list[tuple[float, float, float]] = []
        for iz in range(self.n + 1):
            for ix in range(self.n + 1):
                here = self.at(ix, iz)
                for dx, dz in ((1, 0), (0, 1)):
                    if ix + dx > self.n or iz + dz > self.n:
                        continue
                    ratio = abs(self.at(ix + dx, iz + dz) - here) / self.cell
                    if ratio > max_ratio:
                        found.append((ix * self.cell, iz * self.cell, ratio))
        return found

    def limit_slope(self, max_ratio: float, iterations: int = 24) -> int:
        """기울기가 한계를 넘는 곳을 완만하게 편다.

        인접한 두 점의 높이차가 한계를 넘으면 양쪽을 절반씩 당긴다.
        여러 번 돌리면 급경사가 주변으로 퍼지며 사라진다.
        반환값은 마지막으로 고친 지점 수 — 0 이면 완전히 수렴한 것이다.
        """
        max_delta = max_ratio * self.cell
        fixed = 0

        for _ in range(iterations):
            fixed = 0
            for iz in range(self.n + 1):
                for ix in range(self.n + 1):
                    i = self.index(ix, iz)
                    for dx, dz in ((1, 0), (0, 1)):
                        if ix + dx > self.n or iz + dz > self.n:
                            continue
                        j = self.index(ix + dx, iz + dz)
                        diff = self.heights[j] - self.heights[i]
                        if abs(diff) <= max_delta:
                            continue
                        # 초과분의 절반씩 서로에게 양보한다.
                        excess = (abs(diff) - max_delta) * 0.5
                        sign = 1.0 if diff > 0 else -1.0
                        self.heights[i] += sign * excess
                        self.heights[j] -= sign * excess
                        fixed += 1
            if fixed == 0:
                break

        return fixed

    # --- 지면 칠하기 ---------------------------------------------------------

    def paint_corridor(
        self,
        x0: float,
        z0: float,
        x1: float,
        z1: float,
        half_width: float,
        feather: float = 2.0,
        value: float = 1.0,
    ) -> None:
        """선분을 따라 띠 모양으로 칠한다. 대로에 쓴다."""
        dx, dz = x1 - x0, z1 - z0
        seg_len2 = dx * dx + dz * dz
        if seg_len2 <= 0.0:
            return

        for iz in range(self.n + 1):
            gz = iz * self.cell
            for ix in range(self.n + 1):
                gx = ix * self.cell
                t = max(0.0, min(1.0, ((gx - x0) * dx + (gz - z0) * dz) / seg_len2))
                dist = math.hypot(gx - (x0 + dx * t), gz - (z0 + dz * t))
                if dist > half_width + feather:
                    continue
                w = 1.0 - _smoothstep(half_width, half_width + feather, dist)
                i = self.index(ix, iz)
                self.surface[i] = max(self.surface[i], value * w)

    def paint_box(
        self,
        x: float,
        z: float,
        half_x: float,
        half_z: float,
        feather: float = 2.0,
        value: float = 1.0,
    ) -> None:
        """사각 영역을 칠한다. 광장에 쓴다."""
        for iz in range(self.n + 1):
            gz = iz * self.cell
            for ix in range(self.n + 1):
                gx = ix * self.cell
                dx = max(0.0, abs(gx - x) - half_x)
                dz = max(0.0, abs(gz - z) - half_z)
                dist = math.hypot(dx, dz)
                if dist > feather:
                    continue
                w = 1.0 - _smoothstep(0.0, feather, dist) if feather > 0 else 1.0
                i = self.index(ix, iz)
                self.surface[i] = max(self.surface[i], value * w)

    def carve_channel(
        self,
        points: list[tuple[float, float, float]],
        half_width: float,
        depth: float,
        bank: float,
    ) -> int:
        """물길을 판다. 물에 잠긴 격자점 수를 돌려준다.

        points 는 상류에서 하류로 가는 (x, z, 수면높이) 꺾은선이다.
        수면 높이를 호출자가 주는 이유는 **물이 수평이어야** 하기 때문이다.
        지형을 그대로 따라가게 하면 개울이 언덕을 타고 오른다.

        둑 기울기는 depth / bank 로 **호출자가 정한다.** 이 값이
        MAX_WALK_SLOPE 를 넘으면 그 면이 벽으로 분류되어 캐릭터가 물가에서
        순간이동한다 — 광장 가장자리에서 이미 겪은 문제다.
        물길은 limit_slope **뒤에** 파므로 여기서 만든 경사는 펴지지 않는다.

        지형은 **내리기만** 한다(min). 올리면 이미 놓기로 계산된 지형이
        어긋나고, 무엇보다 개울이 둔덕 위에 얹힌다.
        """
        if len(points) < 2:
            return 0

        segments = []
        for a, b in zip(points, points[1:]):
            dx, dz = b[0] - a[0], b[1] - a[1]
            seg_len2 = dx * dx + dz * dz
            if seg_len2 > 0.0:
                segments.append((a, b, dx, dz, seg_len2))

        reach = half_width + bank
        flooded = 0   # 지형을 깎은 격자점 수. 물 격자점 수는 finish_water() 가 센다.
        for iz in range(self.n + 1):
            gz = iz * self.cell
            for ix in range(self.n + 1):
                gx = ix * self.cell

                # 꺾은선 전체에서 가장 가까운 지점과 그때의 수면 높이.
                best_dist = float("inf")
                best_y = 0.0
                for a, b, dx, dz, seg_len2 in segments:
                    t = max(0.0, min(1.0, ((gx - a[0]) * dx + (gz - a[1]) * dz) / seg_len2))
                    dist = math.hypot(gx - (a[0] + dx * t), gz - (a[1] + dz * t))
                    if dist < best_dist:
                        best_dist = dist
                        best_y = a[2] + (b[2] - a[2]) * t

                if best_dist > reach:
                    continue

                i = self.index(ix, iz)
                bed = best_y - depth
                if best_dist <= half_width:
                    target = bed
                else:
                    # 둑 — 바닥에서 원래 지형으로 직선으로 올린다.
                    t = (best_dist - half_width) / bank
                    target = bed + (max(self.heights[i], best_y + 0.2) - bed) * t

                if target < self.heights[i]:
                    self.heights[i] = target
                    flooded += 1

                # **둑 끝까지** 칠한다. 좁게 칠하면 실제 물가(수면과 둑이
                # 만나는 선)에 못 미친 채 수면이 끊겨 물이 공중에 뜬다.
                # 남는 부분은 finish_water() 가 정리한다.
                self.water[i] = max(self.water[i], best_y)

        return flooded

    def finish_water(self) -> int:
        """수면을 지형에 맞춰 마무리한다. 남은 물 격자점 수를 돌려준다.

        **지형을 다 깎은 뒤 맨 마지막에 한 번만** 부른다. 물길을 판 뒤에도
        지형을 더 손대면(다리 둑 등) 물가가 옮겨지기 때문이다.

        두 가지를 한다:

        1. 지형보다 낮은 수면을 지운다. 그대로 두면 수심이 음수가 되고
           (실측 -6.4m) 셰이더는 수심 0 을 물가로 보아 거품을 칠한다.
        2. 물에 닿은 마른 점을 **한 줄 남긴다.** 수면 높이는 carve_channel 이
           적어 둔 값 그대로 두고, 지형이 그 위를 덮게 한다.

        2번이 핵심이다. 이게 없으면 수면 메시가 물가에 닿기 전에 끊겨
        **물이 공중에 뜬 판자처럼** 보인다 — 실측으로 가장자리가 중앙 0.84m,
        최대 3.64m 떠 있었다. 한 줄을 더 두면 마지막 사각형이 물속에서
        땅속으로 관통하며 끝나고, 눈에 보이는 물가는 **수면과 지형이 만나는
        선**에 저절로 생긴다. 도로를 타일 대신 지면에 칠한 것과 같은 발상이다.

        물가 줄을 지형 높이로 **끌어올리지 않는** 이유: 다리 교대처럼 물가가
        수직인 곳에서 물이 벽을 타고 오른다(2m 벽에 0.9m 짜리 가짜 폭포가 생겼다).
        수면은 수평이어야 한다.
        """
        wet = [w > h + 0.02 for w, h in zip(self.water, self.heights)]
        n = self.n

        # **대각선까지 본다(8방향).** 4방향만 보면 사각형의 한 모서리가 물가
        # 줄에서 빠지고, 클라이언트는 네 꼭짓점이 다 물일 때만 삼각형을 만들므로
        # 그 사각형이 통째로 안 그려진다 — 수면에 구멍이 뚫린다.
        # 실측 49개였고 전부 대각선으로만 물에 닿은 점 48개에서 나왔다.
        neighbours = ((1, 0), (-1, 0), (0, 1), (0, -1),
                      (1, 1), (1, -1), (-1, 1), (-1, -1))
        shore = [False] * len(wet)
        for iz in range(n + 1):
            for ix in range(n + 1):
                i = self.index(ix, iz)
                if wet[i]:
                    continue
                for dx, dz in neighbours:
                    jx, jz = ix + dx, iz + dz
                    if 0 <= jx <= n and 0 <= jz <= n and wet[self.index(jx, jz)]:
                        shore[i] = True
                        break

        alive = 0
        for i in range(len(self.water)):
            if wet[i]:
                alive += 1
            elif shore[i]:
                # carve_channel 이 적어 둔 수면을 그대로 둔다. 지형이 그 위에
                # 있으므로 이 점은 땅속에 묻히고, 물가 선은 두 면이 만나는
                # 곳에 생긴다. 값이 없던 자리만 지형에 맞춘다.
                if self.water[i] <= NO_WATER:
                    self.water[i] = self.heights[i]
                alive += 1
            else:
                self.water[i] = NO_WATER
        return alive

    def water_holes(self) -> int:
        """수면에 뚫린 구멍 수.

        클라이언트(WaterMesh)는 **네 꼭짓점이 모두 물일 때만** 사각형을 그린다.
        물을 품었는데 꼭짓점 하나가 비면 그 칸은 안 그려지고 바닥이 들여다보인다.
        여기서 0 이 나와야 화면에 구멍이 없다.
        """
        n = self.n
        holes = 0
        for iz in range(n):
            for ix in range(n):
                corners = [self.index(ix, iz), self.index(ix + 1, iz),
                           self.index(ix, iz + 1), self.index(ix + 1, iz + 1)]
                if not any(self.water[i] > self.heights[i] + 0.02 for i in corners):
                    continue
                if any(self.water[i] <= NO_WATER for i in corners):
                    holes += 1
        return holes

    def water_cells(self) -> int:
        return sum(1 for w in self.water if w > NO_WATER)

    def to_json(self) -> dict:
        return {
            "cell": self.cell,
            "resolution": self.n + 1,
            "layout": "row-major, index = iz * resolution + ix, 높이는 미터",
            "heights": [round(h, 3) for h in self.heights],
            # 0 = 풀, 1 = 포석. 클라이언트가 정점 색으로 섞는다.
            "surface": [round(s, 2) for s in self.surface],
            # 수면 높이(m). NO_WATER 면 물 없음.
            "water": [round(w, 3) if w > NO_WATER else NO_WATER for w in self.water],
            "no_water": NO_WATER,
        }


# ----------------------------------------------------------------------------
# 지형 구성 요소 — 전부 반경 기반이라 격자 해상도에 좌우되지 않는다
# ----------------------------------------------------------------------------


@dataclass
class Bump:
    """완만한 언덕. 가장자리에서 0 으로 수렴한다."""
    x: float
    z: float
    radius: float
    height: float

    def eval(self, x: float, z: float) -> float:
        d = math.hypot(x - self.x, z - self.z)
        if d >= self.radius:
            return 0.0
        # cos 감쇠 — 가장자리 기울기가 0 이라 이음매가 보이지 않는다.
        return self.height * 0.5 * (1.0 + math.cos(math.pi * d / self.radius))


@dataclass
class Plateau:
    """평평한 대지 + 치맛자락. 광장과 성 언덕에 쓴다.

    height 는 절대 표고가 아니라 **주변 지형 대비 상승량**이다.
    절대값으로 두면 바탕 지형이 올라갈 때 대지가 웅덩이가 된다.
    """
    x: float
    z: float
    radius: float       # 완전히 평평한 반경
    skirt: float        # 여기까지 완만히 내려간다
    height: float

    def weight(self, x: float, z: float) -> float:
        d = math.hypot(x - self.x, z - self.z)
        return 1.0 - _smoothstep(self.radius, self.radius + self.skirt, d)


@dataclass
class BoxPlateau:
    """사각 대지. 광장처럼 사각 타일을 까는 곳에 쓴다.

    원형 대지(Plateau)에 사각 타일을 깔면 경계가 톱니로 보인다.
    타일 격자와 축이 같은 사각 대지를 쓰면 가장자리가 딱 떨어진다.
    Plateau 와 같은 인터페이스(x, z, height, weight)를 가지므로
    build_heightmap 에 섞어 넘길 수 있다.
    """
    x: float
    z: float
    half_x: float
    half_z: float
    skirt: float
    height: float

    def weight(self, x: float, z: float) -> float:
        dx = max(0.0, abs(x - self.x) - self.half_x)
        dz = max(0.0, abs(z - self.z) - self.half_z)
        return 1.0 - _smoothstep(0.0, self.skirt, math.hypot(dx, dz))


def build_heightmap(
    size: float,
    cell: float,
    rng: random.Random,
    *,
    bumps: list[Bump],
    plateaus: list[Plateau],
    global_bumps: list[Bump] | None = None,
    flatten_box: tuple[float, float, float, float] | None = None,
    flatten_skirt: float = 24.0,
    noise_amplitude: float = 0.0,
    noise_scale: float = 40.0,
) -> Heightmap:
    """언덕 + 대지 + 잔잔한 요철을 합친다.

    bumps 는 flatten_box 안쪽(성벽 안)에서 눌린다 — 마을 한복판에 산이
    솟으면 건물이 전부 기울어 보인다.
    global_bumps 와 noise 는 눌리지 않고 어디에나 적용된다. 마을 안도
    완전 평면이면 죽은 판때기로 보이므로 완만한 기복은 남겨야 한다.
    """
    global_bumps = global_bumps or []
    n = int(round(size / cell))

    # 재현 가능한 값 잡음. 격자 꼭짓점마다 난수를 뽑고 쌍선형으로 펼친다.
    coarse = max(2, int(size / noise_scale))
    noise_grid = [
        [rng.uniform(-1.0, 1.0) for _ in range(coarse + 1)] for _ in range(coarse + 1)
    ]

    def noise(x: float, z: float) -> float:
        if noise_amplitude <= 0.0:
            return 0.0
        fx = x / size * coarse
        fz = z / size * coarse
        ix, iz = min(int(fx), coarse - 1), min(int(fz), coarse - 1)
        tx, tz = fx - ix, fz - iz
        tx = tx * tx * (3 - 2 * tx)
        tz = tz * tz * (3 - 2 * tz)
        a = noise_grid[iz][ix] * (1 - tx) + noise_grid[iz][ix + 1] * tx
        b = noise_grid[iz + 1][ix] * (1 - tx) + noise_grid[iz + 1][ix + 1] * tx
        return (a * (1 - tz) + b * tz) * noise_amplitude

    def flat_weight(x: float, z: float) -> float:
        """성벽 안이면 0, 충분히 밖이면 1."""
        if flatten_box is None:
            return 1.0
        x0, x1, z0, z1 = flatten_box
        # 상자 밖으로 나간 거리
        dx = max(x0 - x, 0.0, x - x1)
        dz = max(z0 - z, 0.0, z - z1)
        return _smoothstep(0.0, flatten_skirt, math.hypot(dx, dz))

    def base(x: float, z: float) -> float:
        h = sum(b.eval(x, z) for b in bumps) * flat_weight(x, z)
        h += sum(b.eval(x, z) for b in global_bumps)
        return h + noise(x, z)

    # 대지의 목표 표고는 '그 자리의 바탕 지형 + 상승량' 이다.
    # 대지를 적용하기 전에 중심을 미리 재 둬야 한다.
    targets = [base(p.x, p.z) + p.height for p in plateaus]

    heights: list[float] = []
    for iz in range(n + 1):
        z = iz * cell
        for ix in range(n + 1):
            x = ix * cell
            h = base(x, z)
            # 대지는 성벽 안팎 상관없이 적용된다 — 광장·성이 여기 해당한다.
            for p, target in zip(plateaus, targets):
                w = p.weight(x, z)
                h = h * (1.0 - w) + target * w
            heights.append(h)

    return Heightmap(size=size, cell=cell, heights=heights)
