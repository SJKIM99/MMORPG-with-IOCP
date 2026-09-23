"""world/regions/*.json 을 서버·클라가 함께 읽는 .bin 으로 컴파일한다.

    python tools/build_region.py            # 전부
    python tools/build_region.py town       # 하나만
    python tools/build_region.py --verify   # 만든 뒤 되읽어 대조

JSON 이 소스, .bin 이 산출물이다 (CLAUDE.md 5장). 포맷 정의는 region_format.py.

이 단계가 **검증도 겸한다.** collision / nav 문자열이 오타면 여기서 빌드가
실패한다. 런타임에 조용히 none 으로 떨어지면 "서버에선 통과, 클라에선 벽"
같은 버그가 되는데, 그건 5장이 통째로 막으려는 바로 그 문제다.
"""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

from region_format import (
    COLLISION,
    NAV,
    PLACEMENT_SMOKE,
    PLACEMENT_STRIDE,
    PLACEMENT_STRUCT,
    StringTable,
    Writer,
    decode_strings,
    fnv1a,
    pack_floats,
    read_sections,
    write,
)

REPO = Path(__file__).resolve().parent.parent
SRC_DIR = REPO / "world" / "regions"
OUT_DIR = REPO / "world" / "build"


def _enum(table: dict[str, int], value: str, what: str, where: str) -> int:
    if value not in table:
        raise SystemExit(
            f"[build_region] {where}: 알 수 없는 {what} '{value}'. "
            f"쓸 수 있는 값: {', '.join(sorted(table))}")
    return table[value]


def compile_region(src: Path) -> bytes:
    doc = json.loads(src.read_text(encoding="utf-8"))
    strings = StringTable()
    writer = Writer()

    terrain = doc["terrain"]
    resolution = int(terrain["resolution"])
    heights = terrain["heights"]
    if len(heights) != resolution * resolution:
        raise SystemExit(f"[build_region] {src.name}: heights 길이가 resolution^2 와 다르다")

    height_bytes = pack_floats(heights)

    # 지형 섹션. cell 과 resolution 을 본문 앞에 둬서 섹션만 봐도 해석이 된다.
    writer.add("TERR",
               struct.pack("<If", resolution, float(terrain["cell"])) + height_bytes,
               resolution * resolution)

    # 포장 마스크 — 도로 색을 정점에 섞는 용도라 **클라 전용**이다.
    # 서버는 이 섹션을 건너뛴다. 섹션 표를 쓰는 이유가 이것이다.
    writer.add("SURF", pack_floats(terrain["surface"]), len(terrain["surface"]))

    # 수면. no_water 센티널을 같이 실어 리더가 상수를 중복 정의하지 않게 한다.
    water = terrain.get("water", [])
    if water:
        writer.add("WATR",
                   struct.pack("<f", float(terrain["no_water"])) + pack_floats(water),
                   len(water))

    # --- 배치 ---------------------------------------------------------------
    placements = doc["placements"]
    body = bytearray()
    for i, p in enumerate(placements):
        flags = PLACEMENT_SMOKE if p.get("smoke") else 0
        body += struct.pack(
            PLACEMENT_STRUCT,
            strings.add(p["asset"]),
            *(float(v) for v in p["pos"]),
            float(p["yaw"]), float(p.get("scale", 1.0)),
            _enum(COLLISION, p.get("collision", "none"), "collision", f"{src.name} 배치 {i}"),
            _enum(NAV, p.get("nav", "include"), "nav", f"{src.name} 배치 {i}"),
            flags, 0)
    assert len(body) == len(placements) * PLACEMENT_STRIDE
    writer.add("PLAC", bytes(body), len(placements))

    # --- NPC ----------------------------------------------------------------
    # 가변 길이(idle_variants)라 순차로만 읽을 수 있다. 개수가 적어 문제없다.
    npcs = doc.get("npcs", [])
    body = bytearray()
    for n in npcs:
        variants = n.get("idle_variants", [])
        body += struct.pack("<II" "fff" "f" "III",
                            strings.add(n["id"]), strings.add(n["asset"]),
                            *(float(v) for v in n["pos"]), float(n["yaw"]),
                            strings.add(n.get("role")), strings.add(n.get("animation")),
                            len(variants))
        for v in variants:
            body += struct.pack("<I", strings.add(v))
    writer.add("NPCS", bytes(body), len(npcs))

    # --- 몬스터 스폰 그룹 ------------------------------------------------------
    spawns = doc.get("spawns", [])
    body = bytearray()
    for s in spawns:
        body += struct.pack("<III" "fff" "f" "II",
                            strings.add(s["id"]), strings.add(s.get("monster")),
                            strings.add(s.get("asset")),
                            *(float(v) for v in s["center"]),
                            float(s["radius"]), int(s["count"]), int(s.get("respawn_ms", 0)))
    writer.add("SPWN", bytes(body), len(spawns))

    # --- 순찰 경로 -------------------------------------------------------------
    routes = doc.get("patrol_routes", [])
    body = bytearray()
    for r in routes:
        points = r["waypoints"]
        body += struct.pack("<III", strings.add(r["id"]), 1 if r.get("loop") else 0, len(points))
        for wp in points:
            body += pack_floats([float(v) for v in wp])
    writer.add("PATH", bytes(body), len(routes))

    # --- 색 (클라 전용) ---------------------------------------------------------
    ground = doc.get("ground", {})
    water_info = doc.get("water", {})
    writer.add("COLR", struct.pack(
        "<IIIII",
        strings.add(ground.get("color")), strings.add(ground.get("road_color")),
        strings.add(water_info.get("color")), strings.add(water_info.get("deep_color")),
        strings.add(water_info.get("foam_color"))), 5)

    # 헤더가 참조하는 문자열도 **인코딩 전에** 넣어야 한다.
    # 처음에 writer.build() 인자 안에서 add 했다가 표에 안 들어가 --verify 가
    # IndexError 로 잡았다. 표를 굳히는 시점이 하나뿐이라는 것을 잊기 쉽다.
    id_str = strings.add(doc["id"])
    display_str = strings.add(doc.get("display_name"))

    # 문자열 표는 **맨 마지막에** 넣는다. 위 섹션들이 만들면서 계속 추가하기 때문이다.
    table_body, table_count = strings.encode()
    writer.add("STRS", table_body, table_count)

    y_range = doc.get("y_range", [0, 64])
    flags = doc.get("flags", {})
    return writer.build({
        "terrain_hash": fnv1a(height_bytes),
        "size_x": float(doc["size"][0]), "size_z": float(doc["size"][1]),
        "sector_size": float(doc["sector_size"]),
        "spawn": tuple(float(v) for v in doc["spawn_point"]),
        "y_min": float(y_range[0]), "y_max": float(y_range[1]),
        "seed": int(doc.get("seed", 0)),
        "pvp": flags.get("pvp", False),
        "spawn_allowed": flags.get("spawn", False),
        "id_str": id_str,
        "display_str": display_str,
    })


def verify(path: Path, src: Path) -> None:
    """되읽어 원본 JSON 과 대조한다. 기록기 자신의 버그를 잡기 위한 것이다."""
    raw = path.read_bytes()
    header, sections = read_sections(raw)
    doc = json.loads(src.read_text(encoding="utf-8"))

    offset, _, count = sections["TERR"]
    resolution, cell = struct.unpack_from("<If", raw, offset)
    body = raw[offset + 8: offset + 8 + count * 4]
    if fnv1a(body) != header["terrain_hash"]:
        raise SystemExit(f"[build_region] {path.name}: 지형 해시 불일치")
    if resolution != doc["terrain"]["resolution"] or abs(cell - doc["terrain"]["cell"]) > 1e-6:
        raise SystemExit(f"[build_region] {path.name}: 지형 머리값 불일치")

    stroff, _, strcount = sections["STRS"]
    strings = decode_strings(raw, stroff, strcount)
    if strings[header["id_str"]] != doc["id"]:
        raise SystemExit(f"[build_region] {path.name}: id 불일치")

    offset, _, pcount = sections["PLAC"]
    if pcount != len(doc["placements"]):
        raise SystemExit(f"[build_region] {path.name}: 배치 수 불일치")
    # 첫 배치와 마지막 배치를 좌표까지 대조한다.
    for idx in (0, pcount - 1):
        asset, px, py, pz = struct.unpack_from("<Ifff", raw, offset + idx * PLACEMENT_STRIDE)
        want = doc["placements"][idx]
        if strings[asset] != want["asset"]:
            raise SystemExit(f"[build_region] {path.name}: 배치 {idx} 에셋 불일치")
        for got, expect, axis in ((px, want["pos"][0], "x"), (py, want["pos"][1], "y"),
                                  (pz, want["pos"][2], "z")):
            if abs(got - expect) > 1e-3:
                raise SystemExit(
                    f"[build_region] {path.name}: 배치 {idx} {axis} {got} != {expect}")


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    do_verify = "--verify" in sys.argv

    names = args or ["town", "field_01"]
    for name in names:
        src = SRC_DIR / f"{name}.json"
        if not src.exists():
            raise SystemExit(f"[build_region] 없는 리전: {src}")

        data = compile_region(src)
        out = OUT_DIR / f"{name}.bin"
        write(out, data)

        header, sections = read_sections(data)
        tags = " ".join(f"{t}:{c}" for t, (_, _, c) in sections.items())
        print(f"{name:10s} {src.stat().st_size / 1048576:5.2f} MB (JSON)"
              f" -> {len(data) / 1048576:5.2f} MB (bin)"
              f"  해시 {header['terrain_hash']:08x}")
        print(f"           {tags}")

        if do_verify:
            verify(out, src)
            print(f"           대조 통과")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
