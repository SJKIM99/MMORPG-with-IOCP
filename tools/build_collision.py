"""리전 데이터에서 Recast 입력용 콜리전 메시(.obj)를 뽑는다.

    python tools/build_collision.py            # 전부
    python tools/build_collision.py town       # 하나만

    world/build/<region>.bin  ->  world/build/<region>.obj

CLAUDE.md 7장의 파이프라인 첫 칸이다. 빌드는 오프라인, 쿼리만 런타임.

**클라이언트가 만드는 콜라이더와 같은 모양을 뽑는다.**
Client/World/CollisionBuilder.cs 를 그대로 옮긴 것이다 — 모양이 어긋나면
"클라에선 벽, 서버 내비메시에선 통과" 가 생기는데, 그게 5장이 통째로 막으려는
바로 그 문제다. 둘 다 같은 .bin 과 같은 manifest 를 읽고 같은 규칙으로 만든다.

OBJ 의 usemtl 로 삼각형의 용도를 나눈다. Recast 빌더가 이 그룹을 보고
area id 를 정한다:

    walkable   걸을 수 있는 면 (지형, 계단, 다리)
    blocker    막는 것 (건물, 성벽, 나무 줄기)
    water      물 — 걸을 수 없다 (5장의 nav: exclude)
"""

from __future__ import annotations

import json
import math
import struct
import sys
from pathlib import Path

from gltf_bounds import Bounds, asset_path, load_manifest, read_bounds

REPO = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO / "world" / "build"

# Vec3.h 와 같은 규약: 오른손, Y-up, 지면은 XZ.
NO_WATER = -1000.0


class ObjWriter:
    """삼각형을 모아 OBJ 로 쓴다. 정점을 재사용하지 않는다 — Recast 는
    인덱스 효율을 신경 쓰지 않고, 중복 제거를 넣으면 버그만 늘어난다."""

    def __init__(self) -> None:
        self._vertices: list[tuple[float, float, float]] = []
        self._groups: dict[str, list[tuple[int, int, int]]] = {}

    def add_triangle(self, group: str, a, b, c) -> None:
        base = len(self._vertices) + 1          # OBJ 인덱스는 1부터
        self._vertices.extend((a, b, c))
        self._groups.setdefault(group, []).append((base, base + 1, base + 2))

    def add_quad(self, group: str, a, b, c, d) -> None:
        """a-b-c-d 를 두 삼각형으로. 인자는 (-x-z, +x-z, -x+z, +x+z) 순서다.

        **감는 방향이 Godot 과 반대다.** Recast 는 삼각형의 법선으로 경사를
        재는데, 오른손 규약(반시계가 앞면)이라 Godot 의 지면 메시와 같은 순서로
        감으면 법선이 **아래**를 향한다. 그러면 모든 지면이 180도 경사로 잡혀
        "걸을 수 없음" 이 된다 — 실제로 마을이 9폴리곤, 필드가 0폴리곤으로
        나왔다. 위에서 볼 때 반시계가 되도록 b 와 c 를 바꾼다.
        """
        self.add_triangle(group, a, c, b)
        self.add_triangle(group, b, c, d)

    def add_box(self, group: str, center, half, yaw: float) -> None:
        """yaw 만 회전한 상자. 12 삼각형."""
        cos, sin = math.cos(yaw), math.sin(yaw)

        def corner(sx: int, sy: int, sz: int):
            lx, ly, lz = sx * half[0], sy * half[1], sz * half[2]
            return (center[0] + lx * cos + lz * sin,
                    center[1] + ly,
                    center[2] - lx * sin + lz * cos)

        p = {(sx, sy, sz): corner(sx, sy, sz)
             for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)}

        faces = [
            # (네 모서리) — 바깥을 향하도록 감는다
            ((-1, 1, -1), (1, 1, -1), (-1, 1, 1), (1, 1, 1)),      # 윗면
            ((-1, -1, 1), (1, -1, 1), (-1, -1, -1), (1, -1, -1)),  # 아랫면
            ((-1, -1, -1), (1, -1, -1), (-1, 1, -1), (1, 1, -1)),  # -Z
            ((1, -1, 1), (-1, -1, 1), (1, 1, 1), (-1, 1, 1)),      # +Z
            ((-1, -1, 1), (-1, -1, -1), (-1, 1, 1), (-1, 1, -1)),  # -X
            ((1, -1, -1), (1, -1, 1), (1, 1, -1), (1, 1, 1)),      # +X
        ]
        for f in faces:
            self.add_quad(group, p[f[0]], p[f[1]], p[f[2]], p[f[3]])

    def add_cylinder(self, group: str, center, radius: float, height: float,
                     segments: int = 8) -> None:
        """세로 원기둥. 나무 줄기용이라 옆면과 윗면만 있으면 충분하다."""
        bottom = center[1]
        top = center[1] + height
        ring = []
        for i in range(segments):
            angle = math.tau * i / segments
            ring.append((center[0] + math.cos(angle) * radius,
                         center[2] + math.sin(angle) * radius))

        for i in range(segments):
            x0, z0 = ring[i]
            x1, z1 = ring[(i + 1) % segments]
            self.add_quad(group,
                          (x0, bottom, z0), (x1, bottom, z1),
                          (x0, top, z0), (x1, top, z1))
            # 윗면 (부채꼴)
            self.add_triangle(group, (center[0], top, center[2]),
                              (x0, top, z0), (x1, top, z1))

    def triangle_count(self) -> dict[str, int]:
        return {g: len(f) for g, f in self._groups.items()}

    def write(self, path: Path, comment: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        out = [f"# {comment}",
               "# tools/build_collision.py 생성. 손으로 고치지 말 것.",
               "# 단위 1 = 1m, 오른손 Y-up, 지면은 XZ (CLAUDE.md 3장).", ""]
        for x, y, z in self._vertices:
            out.append(f"v {x:.4f} {y:.4f} {z:.4f}")
        for group, faces in self._groups.items():
            out.append("")
            out.append(f"usemtl {group}")
            out.append(f"g {group}")
            for a, b, c in faces:
                out.append(f"f {a} {b} {c}")
        path.write_text("\n".join(out) + "\n", encoding="utf-8")


# ----------------------------------------------------------------------------
# 리전 바이너리 읽기 (region_format.py 와 같은 레이아웃)
# ----------------------------------------------------------------------------


def read_region(path: Path) -> dict:
    from region_format import PLACEMENT_STRIDE, decode_strings, read_sections

    raw = path.read_bytes()
    header, sections = read_sections(raw)

    offset, _, count = sections["TERR"]
    resolution, cell = struct.unpack_from("<If", raw, offset)
    heights = list(struct.unpack_from(f"<{count}f", raw, offset + 8))

    water: list[float] = []
    no_water = NO_WATER
    if "WATR" in sections:
        offset, _, wcount = sections["WATR"]
        (no_water,) = struct.unpack_from("<f", raw, offset)
        water = list(struct.unpack_from(f"<{wcount}f", raw, offset + 4))

    stroff, _, strcount = sections["STRS"]
    strings = decode_strings(raw, stroff, strcount)

    placements = []
    if "PLAC" in sections:
        offset, _, pcount = sections["PLAC"]
        for i in range(pcount):
            asset, px, py, pz, yaw, scale, collision, nav, flags, _pad = struct.unpack_from(
                "<Ifffff" "BBBB", raw, offset + i * PLACEMENT_STRIDE)
            placements.append({
                "asset": strings[asset], "pos": (px, py, pz),
                "yaw": yaw, "scale": scale,
                "collision": collision, "nav": nav,
            })

    return {
        "id": strings[header["id_str"]],
        "size": (header["size_x"], header["size_z"]),
        "resolution": resolution, "cell": cell,
        "heights": heights, "water": water, "no_water": no_water,
        "placements": placements,
    }


# ----------------------------------------------------------------------------
# 배치 -> 삼각형
# ----------------------------------------------------------------------------

COLLISION_NONE, COLLISION_WALKABLE, COLLISION_BLOCKER = 0, 1, 2


def emit_placement(obj: ObjWriter, table, p: dict) -> bool:
    """CollisionBuilder.Attach 와 같은 규칙으로 모양을 만든다. 만들었으면 True."""
    if p["collision"] == COLLISION_NONE:
        return False

    group = "walkable" if p["collision"] == COLLISION_WALKABLE else "blocker"
    entry = table.entry(p["asset"])
    bounds = table.bounds(p["asset"])
    if bounds is None:
        return False

    scale = p["scale"]
    pos = p["pos"]
    yaw = p["yaw"]
    spec = entry.get("collider") or {}
    shape = spec.get("shape", "aabb")

    if shape == "cylinder":
        # 나무 — AABB 를 쓰면 수관이 통째로 벽이 되어 숲이 막힌다.
        radius = float(spec["radius"]) * scale
        height = bounds.size[1] * scale
        obj.add_cylinder(group, (pos[0], pos[1], pos[2]), radius, height)
        return True

    if shape == "ramp":
        # 계단 — 시각 메시는 단차지만 콜리전은 경사면 하나다.
        # 그래야 MoveAndSlide 가 미끄러뜨려 올리고, Recast 도 걸을 수 있는
        # 면으로 인식한다. 단차로 두면 폴리곤이 잘게 쪼개지고 경로가 끊긴다.
        rise = bounds.size[1] * scale
        run = bounds.size[2] * scale
        width = bounds.size[0] * scale
        angle = math.atan2(rise, run)
        slope = math.hypot(rise, run)
        thickness = 0.5

        # 모델 로컬 +Z 가 내리막. 경사면 중심은 시작점에서 절반 내려간 곳이다.
        mid_local = (0.0, rise * 0.5, run * 0.5)
        cos_y, sin_y = math.cos(yaw), math.sin(yaw)
        center = (pos[0] + mid_local[0] * cos_y + mid_local[2] * sin_y,
                  pos[1] + mid_local[1],
                  pos[2] - mid_local[0] * sin_y + mid_local[2] * cos_y)

        # yaw 만 회전하는 상자로는 pitch 를 줄 수 없으므로 삼각형을 직접 놓는다.
        half_w = width * 0.5
        dx, dz = math.sin(yaw), math.cos(yaw)          # 내리막 방향(XZ)
        px, pz = math.cos(yaw), -math.sin(yaw)         # 폭 방향
        half_len = slope * 0.5
        top_y = center[1] + math.cos(angle) * 0.0 + math.sin(angle) * half_len
        bot_y = center[1] - math.sin(angle) * half_len

        a = (center[0] - dx * half_len - px * half_w, top_y, center[2] - dz * half_len - pz * half_w)
        b = (center[0] - dx * half_len + px * half_w, top_y, center[2] - dz * half_len + pz * half_w)
        c = (center[0] + dx * half_len - px * half_w, bot_y, center[2] + dz * half_len - pz * half_w)
        d = (center[0] + dx * half_len + px * half_w, bot_y, center[2] + dz * half_len + pz * half_w)
        obj.add_quad(group, a, b, c, d)
        _ = thickness
        return True

    if shape == "mesh":
        # 성문·다리 — 구멍이 있어 상자로 덮으면 통로가 막힌다.
        # 실제 삼각형을 그대로 싣는다.
        return emit_gltf_mesh(obj, group, table, p)

    # 기본: AABB 상자.
    size = [v * scale for v in bounds.size]
    # bounds.min 은 모델 원점 기준이므로 중심을 다시 잡는다.
    center_local = [(bounds.min[i] + bounds.size[i] * 0.5) * scale for i in range(3)]
    cos_y, sin_y = math.cos(yaw), math.sin(yaw)
    center = (pos[0] + center_local[0] * cos_y + center_local[2] * sin_y,
              pos[1] + center_local[1],
              pos[2] - center_local[0] * sin_y + center_local[2] * cos_y)
    obj.add_box(group, center, (size[0] * 0.5, size[1] * 0.5, size[2] * 0.5), yaw)
    return True


def emit_gltf_mesh(obj: ObjWriter, group: str, table, p: dict) -> bool:
    """glTF 의 삼각형을 그대로 배치 변환에 태워 싣는다."""
    tris = table.triangles(p["asset"])
    if not tris:
        return False

    scale = p["scale"]
    pos = p["pos"]
    cos_y, sin_y = math.cos(p["yaw"]), math.sin(p["yaw"])

    def place(v):
        x, y, z = v[0] * scale, v[1] * scale, v[2] * scale
        return (pos[0] + x * cos_y + z * sin_y,
                pos[1] + y,
                pos[2] - x * sin_y + z * cos_y)

    for a, b, c in tris:
        obj.add_triangle(group, place(a), place(b), place(c))
    return True


# ----------------------------------------------------------------------------


class AssetGeometry:
    """manifest + 실측 바운딩 + (필요할 때만) 삼각형."""

    def __init__(self, repo: Path):
        self.repo = repo
        self.manifest = load_manifest(repo)
        self._bounds: dict[str, Bounds | None] = {}
        self._tris: dict[str, list] = {}

    def entry(self, asset_id: str) -> dict:
        return self.manifest["assets"][asset_id]

    def bounds(self, asset_id: str) -> Bounds | None:
        if asset_id not in self._bounds:
            self._bounds[asset_id] = read_bounds(
                asset_path(self.repo, self.manifest, asset_id))
        return self._bounds[asset_id]

    def triangles(self, asset_id: str) -> list:
        if asset_id in self._tris:
            return self._tris[asset_id]

        path = asset_path(self.repo, self.manifest, asset_id)
        raw = path.read_bytes()
        offset = 12
        gltf, blob = None, b""
        while offset < len(raw):
            length, kind = struct.unpack_from("<II", raw, offset)
            chunk = raw[offset + 8: offset + 8 + length]
            if kind == 0x4E4F534A:
                gltf = json.loads(chunk)
            elif kind == 0x004E4942:
                blob = chunk
            offset += 8 + length

        tris: list = []
        if gltf is not None:
            for mesh in gltf.get("meshes", []):
                for prim in mesh["primitives"]:
                    verts = _accessor_vec3(gltf, blob, prim["attributes"]["POSITION"])
                    if "indices" in prim:
                        idx = _accessor_scalar(gltf, blob, prim["indices"])
                    else:
                        idx = list(range(len(verts)))
                    for i in range(0, len(idx) - 2, 3):
                        tris.append((verts[idx[i]], verts[idx[i + 1]], verts[idx[i + 2]]))

        self._tris[asset_id] = tris
        return tris


def _view(gltf, accessor):
    view = gltf["bufferViews"][accessor["bufferView"]]
    return view.get("byteOffset", 0) + accessor.get("byteOffset", 0), view.get("byteStride")


def _accessor_vec3(gltf, blob, index):
    acc = gltf["accessors"][index]
    start, stride = _view(gltf, acc)
    stride = stride or 12
    return [struct.unpack_from("<3f", blob, start + i * stride) for i in range(acc["count"])]


def _accessor_scalar(gltf, blob, index):
    acc = gltf["accessors"][index]
    start, stride = _view(gltf, acc)
    fmt = {5121: "<B", 5123: "<H", 5125: "<I"}[acc["componentType"]]
    size = struct.calcsize(fmt)
    stride = stride or size
    return [struct.unpack_from(fmt, blob, start + i * stride)[0] for i in range(acc["count"])]


# ----------------------------------------------------------------------------


def build(region_id: str) -> None:
    src = BUILD_DIR / f"{region_id}.bin"
    if not src.exists():
        raise SystemExit(f"[build_collision] {src} 가 없다 — build_region.py 를 먼저 돌려라")

    region = read_region(src)
    table = AssetGeometry(REPO)
    obj = ObjWriter()

    res = region["resolution"]
    cell = region["cell"]
    heights = region["heights"]
    water = region["water"]
    no_water = region["no_water"]

    def height_at(ix: int, iz: int) -> float:
        return heights[iz * res + ix]

    def submerged(ix: int, iz: int) -> bool:
        if not water:
            return False
        i = iz * res + ix
        return water[i] > no_water and water[i] > heights[i] + 0.02

    # --- 지형 -------------------------------------------------------------
    # 물에 잠긴 칸은 **빼 버린다.** 그러면 내비메시에 구멍이 생기고, 그게
    # 5장의 nav: exclude 를 그대로 구현한 것이 된다. 나중에 얕은 물을 걸어서
    # 건너게 하려면 여기서 area id 를 따로 주면 된다.
    land = 0
    flooded = 0
    for iz in range(res - 1):
        for ix in range(res - 1):
            if (submerged(ix, iz) and submerged(ix + 1, iz)
                    and submerged(ix, iz + 1) and submerged(ix + 1, iz + 1)):
                flooded += 1
                continue

            x0, x1 = ix * cell, (ix + 1) * cell
            z0, z1 = iz * cell, (iz + 1) * cell
            obj.add_quad("walkable",
                         (x0, height_at(ix, iz), z0),
                         (x1, height_at(ix + 1, iz), z0),
                         (x0, height_at(ix, iz + 1), z1),
                         (x1, height_at(ix + 1, iz + 1), z1))
            land += 1

    # --- 배치 -------------------------------------------------------------
    emitted = 0
    skipped: dict[str, int] = {}
    for p in region["placements"]:
        try:
            if emit_placement(obj, table, p):
                emitted += 1
        except Exception as exc:                     # noqa: BLE001
            skipped[p["asset"]] = skipped.get(p["asset"], 0) + 1
            if len(skipped) < 4:
                print(f"  건너뜀 {p['asset']}: {exc}")

    out = BUILD_DIR / f"{region_id}.obj"
    counts = obj.triangle_count()
    obj.write(out, f"{region_id} collision mesh")

    total = sum(counts.values())
    print(f"{region_id:10s} 지형 {land}칸(물 제외 {flooded}칸)  배치 {emitted}개")
    print(f"           삼각형 {total:,}  " + "  ".join(f"{g} {n:,}" for g, n in counts.items())
          + f"  -> {out.relative_to(REPO)}  {out.stat().st_size / 1048576:.1f} MB")
    if skipped:
        print(f"           건너뛴 에셋 {len(skipped)}종")


def main() -> int:
    names = [a for a in sys.argv[1:] if not a.startswith("--")] or ["town", "field_01"]
    for name in names:
        build(name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
