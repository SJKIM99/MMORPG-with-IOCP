"""glTF / GLB 의 바운딩 박스를 외부 의존성 없이 읽는다.

배치 생성기가 이 값을 쓴다:
  - min.y  : 모델 원점이 바닥인지 중심인지 판별 → 지면 스냅 보정량
  - x/z 범위: 프롭 간 최소 간격과 도로 침범 판정에 쓰는 반경

POSITION 접근자의 min/max 만 읽으므로 정점 버퍼를 파싱하지 않는다.
노드 트랜스폼은 적용하지 않는다 — KayKit/Quaternius 모델은 메시 노드가
단위 트랜스폼이라 문제되지 않지만, 다른 팩을 넣을 때는 확인이 필요하다.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

_GLB_MAGIC = 0x46546C67  # 'glTF'
_CHUNK_JSON = 0x4E4F534A  # 'JSON'


@dataclass(frozen=True)
class Bounds:
    min: tuple[float, float, float]
    max: tuple[float, float, float]

    @property
    def size(self) -> tuple[float, float, float]:
        return tuple(self.max[i] - self.min[i] for i in range(3))

    @property
    def footprint_radius(self) -> float:
        """XZ 평면에서 원점을 중심으로 한 외접 반경. 간격 판정용."""
        return max(
            abs(self.min[0]), abs(self.max[0]), abs(self.min[2]), abs(self.max[2])
        )

    @property
    def ground_offset(self) -> float:
        """이 모델을 y=0 지면에 앉히려면 더해야 하는 y 값."""
        return -self.min[1]


def _read_gltf_json(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) >= 12 and struct.unpack_from("<I", data, 0)[0] == _GLB_MAGIC:
        offset = 12
        while offset + 8 <= len(data):
            length, kind = struct.unpack_from("<II", data, offset)
            offset += 8
            if kind == _CHUNK_JSON:
                return json.loads(data[offset : offset + length])
            offset += length
        raise ValueError(f"GLB 에 JSON 청크가 없다: {path}")
    return json.loads(data)


def read_bounds(path: Path) -> Bounds | None:
    """모든 메시 프리미티브의 POSITION min/max 를 합친 바운딩 박스."""
    doc = _read_gltf_json(path)
    accessors = doc.get("accessors", [])

    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    found = False

    for mesh in doc.get("meshes", []):
        for prim in mesh.get("primitives", []):
            index = prim.get("attributes", {}).get("POSITION")
            if index is None:
                continue
            accessor = accessors[index]
            a_min, a_max = accessor.get("min"), accessor.get("max")
            if not a_min or not a_max:
                continue
            found = True
            for i in range(3):
                lo[i] = min(lo[i], float(a_min[i]))
                hi[i] = max(hi[i], float(a_max[i]))

    if not found:
        return None
    return Bounds(tuple(lo), tuple(hi))


def load_manifest(repo_root: Path) -> dict:
    return json.loads((repo_root / "world" / "manifest.json").read_text("utf-8"))


def asset_path(repo_root: Path, manifest: dict, asset_id: str) -> Path:
    entry = manifest["assets"][asset_id]
    root = manifest["sources"][entry["source"]]["root"]
    return repo_root / root / entry["file"]


def measure_all(repo_root: Path, manifest: dict) -> dict[str, Bounds]:
    """manifest 의 모든 에셋을 실측한다. scale 은 적용하지 않은 원본 치수."""
    out: dict[str, Bounds] = {}
    for asset_id in manifest["assets"]:
        bounds = read_bounds(asset_path(repo_root, manifest, asset_id))
        if bounds is not None:
            out[asset_id] = bounds
    return out


if __name__ == "__main__":
    repo = Path(__file__).resolve().parent.parent
    mani = load_manifest(repo)
    print(f"{'asset':<26}{'size (x,y,z)':<26}{'min.y':>8}{'radius':>8}  pivot")
    print("-" * 86)
    for aid, b in measure_all(repo, mani).items():
        sx, sy, sz = b.size
        pivot = "바닥" if abs(b.min[1]) < 0.01 else ("중심" if b.min[1] < -0.01 else "공중")
        print(
            f"{aid:<26}{sx:7.2f} {sy:7.2f} {sz:7.2f}      "
            f"{b.min[1]:8.2f}{b.footprint_radius:8.2f}  {pivot}"
        )
