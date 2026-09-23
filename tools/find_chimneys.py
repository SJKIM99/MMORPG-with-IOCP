"""건물 메시에서 굴뚝 위치를 찾아 manifest 에 기록한다.

    python tools/find_chimneys.py            # 찾아서 보여주기만
    python tools/find_chimneys.py --write    # manifest 에 기록

왜 도구로 만드는가 — CLAUDE.md 5장. 클라이언트가 실행할 때마다 메시를
뒤져 굴뚝을 찾으면 그건 코드다. 한 번 찾아서 manifest 에 적어 두면 데이터고,
나중에 서버가 같은 값을 읽을 수도 있다.

찾는 방법:
    KayKit 건물은 **머티리얼별로** 프리미티브가 나뉘어 있다. 부위별이 아니다.
    그래서 "굴뚝 노드"는 존재하지 않는다. 대신 이 성질을 쓴다 —
    **굴뚝은 석재(Stone)인데 지붕(Brown)보다 높이 솟은 유일한 부위다.**

    1. 머티리얼 이름에 Stone 이 들어간 프리미티브의 정점을 모은다
    2. 지붕(Brown 계열) 최고점보다 높은 정점만 남긴다
    3. XZ 로 뭉치면(격자 클러스터링) 굴뚝 하나가 된다
    4. 뭉친 것의 폭이 굴뚝치고 너무 넓으면(탑, 박공벽) 버린다

굴뚝이 여러 개인 건물도 있어서 클러스터링이 필요하다. 실제로 막사(barracks)는
상단 석재가 3.7m 로 퍼져 있어 한 덩어리로 보면 굴뚝이 아닌 것으로 걸러진다.
"""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

from gltf_bounds import asset_path, load_manifest

REPO = Path(__file__).resolve().parent.parent

# 굴뚝으로 인정할 최대 폭(로컬 단위). 배율 6 이면 월드 2.4m 다.
MAX_CHIMNEY_SPAN = 0.40
# 지붕보다 이만큼은 솟아야 굴뚝이다.
MIN_RISE_OVER_ROOF = 0.03
# 클러스터 격자 크기. 굴뚝 하나가 쪼개지지 않을 만큼 크게 잡는다.
CLUSTER_CELL = 0.45


def read_glb(path: Path) -> tuple[dict, bytes]:
    raw = path.read_bytes()
    if raw[:4] != b"glTF":
        raise ValueError(f"glb 가 아니다: {path}")
    offset = 12
    js: dict = {}
    blob = b""
    while offset < len(raw):
        length, kind = struct.unpack_from("<II", raw, offset)
        chunk = raw[offset + 8 : offset + 8 + length]
        if kind == 0x4E4F534A:
            js = json.loads(chunk)
        elif kind == 0x004E4942:
            blob = chunk
        offset += 8 + length
    return js, blob


def positions(gltf: dict, blob: bytes, accessor_index: int) -> list[tuple[float, float, float]]:
    acc = gltf["accessors"][accessor_index]
    view = gltf["bufferViews"][acc["bufferView"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or 12
    return [struct.unpack_from("<3f", blob, start + i * stride) for i in range(acc["count"])]


def find_chimneys(gltf: dict, blob: bytes) -> list[tuple[float, float, float]]:
    materials = [m.get("name", "") for m in gltf.get("materials", [])]

    roof_top = None
    stone: list[tuple[float, float, float]] = []
    for mesh in gltf.get("meshes", []):
        for prim in mesh["primitives"]:
            name = materials[prim["material"]] if "material" in prim else ""
            acc = gltf["accessors"][prim["attributes"]["POSITION"]]
            if "Brown" in name or "Wood" in name:
                top = acc["max"][1]
                roof_top = top if roof_top is None else max(roof_top, top)
            if "Stone" in name:
                stone.extend(positions(gltf, blob, prim["attributes"]["POSITION"]))

    if roof_top is None or not stone:
        return []

    above = [v for v in stone if v[1] > roof_top + MIN_RISE_OVER_ROOF]
    if not above:
        return []

    # XZ 격자로 뭉친다. 굴뚝이 두 개면 두 덩어리가 나온다.
    buckets: dict[tuple[int, int], list[tuple[float, float, float]]] = {}
    for v in above:
        key = (int(v[0] // CLUSTER_CELL), int(v[2] // CLUSTER_CELL))
        buckets.setdefault(key, []).append(v)

    # 인접한 칸을 이어 붙인다 (굴뚝 하나가 칸 경계에 걸칠 수 있다).
    merged: list[list[tuple[float, float, float]]] = []
    seen: set[tuple[int, int]] = set()
    for key in buckets:
        if key in seen:
            continue
        stack = [key]
        group: list[tuple[float, float, float]] = []
        while stack:
            k = stack.pop()
            if k in seen or k not in buckets:
                continue
            seen.add(k)
            group.extend(buckets[k])
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    stack.append((k[0] + dx, k[1] + dz))
        merged.append(group)

    found = []
    for group in merged:
        xs = [v[0] for v in group]
        zs = [v[2] for v in group]
        span = max(max(xs) - min(xs), max(zs) - min(zs))
        if span > MAX_CHIMNEY_SPAN:
            continue
        found.append((
            round(sum(xs) / len(xs), 4),
            round(max(v[1] for v in group), 4),
            round(sum(zs) / len(zs), 4),
        ))
    return found


def main() -> int:
    write = "--write" in sys.argv
    manifest = load_manifest(REPO)
    changed = 0

    for asset_id in sorted(manifest["assets"]):
        entry = manifest["assets"][asset_id]
        if entry.get("kind") != "building":
            continue

        gltf, blob = read_glb(asset_path(REPO, manifest, asset_id))
        spots = find_chimneys(gltf, blob)
        scale = float(entry.get("scale", 1.0))

        if not spots:
            entry.pop("chimney", None)
            continue

        world = [[round(c * scale, 3) for c in s] for s in spots]
        print(f"{asset_id:20s} 굴뚝 {len(spots)}개  월드 오프셋 {world}")
        entry["chimney"] = spots
        changed += 1

    if write:
        path = REPO / "world" / "manifest.json"
        path.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"\nmanifest 갱신: 건물 {changed}종에 chimney 기록")
    else:
        print(f"\n건물 {changed}종에서 찾았다. 기록하려면 --write")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
