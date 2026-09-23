"""리전 바이너리 포맷 (.bin) 의 정의와 기록기.

**이 파일이 포맷의 원본이다.** C++ 리더(GameServer/World/RegionData.h)와
C# 리더(Client/World/RegionBinary.cs)는 여기 적힌 것을 그대로 옮긴 것이다.
셋 중 하나가 어긋나면 헤더의 terrain_hash 대조에서 즉시 걸린다.

왜 바이너리인가
---------------
JSON 은 사람이 읽고 git 이 diff 하라고 있는 것이고, 그건 소스인
world/regions/*.json 이 계속 담당한다. 여기서 만드는 .bin 은 **빌드 산출물**이다
(CLAUDE.md 5장의 build/town.obj, build/town.navmesh 와 같은 층위).

바이너리로 가는 이유는 크기가 아니다 — town.json 1.10MB -> 0.24MB 는 시작할 때
한 번 읽는 파일로서는 의미 없는 차이다. **진짜 이유는 서버에 JSON 파서가 없다는
것이다.** 넣으려면 새 라이브러리(nlohmann/rapidjson)를 들여야 하고 그건 2장의
승인 대상이다. 바이너리 리더는 의존성 0 에 40줄이면 끝난다.

그리고 서버와 클라가 **같은 .bin 을 읽는다.** 5장의 "같은 JSON 을 읽는다"보다
강한 보장이다 — 파서가 하나면 어긋날 수가 없다.

레이아웃
--------
전부 리틀엔디안, 4바이트 정렬. x86/x64 에서만 돌므로 바이트 순서를 뒤집지 않는다.

    [헤더 64바이트]
      char[4]  magic          "RGN1"
      u32      version        FORMAT_VERSION
      u32      section_count
      u32      terrain_hash   지형 높이 원시 바이트의 FNV-1a
      f32      size_x, size_z
      f32      sector_size
      f32      spawn_x, spawn_y, spawn_z
      f32      y_min, y_max
      u32      seed
      u8       pvp, spawn_allowed, _pad, _pad
      u32      id_str         문자열 표 인덱스
      u32      display_str

    [섹션 표 section_count x 16바이트]
      char[4]  tag
      u32      offset         파일 처음부터의 바이트 오프셋
      u32      bytes
      u32      count          원소 수 (섹션마다 의미가 다르다)

    [섹션 본문들]

섹션 표를 쓰는 이유: 서버는 SURF(포장 색)처럼 필요 없는 섹션을 건너뛸 수 있고,
나중에 섹션을 추가해도 그걸 모르는 옛 리더가 깨지지 않는다.
"""

from __future__ import annotations

import struct
from pathlib import Path

MAGIC = b"RGN1"

# 헤더 한 줄 정의. 세 언어가 이 순서를 그대로 따라야 한다.
#   magic, version, section_count, terrain_hash,
#   size_x, size_z, sector_size, spawn[3], y_min, y_max, seed,
#   pvp, spawn_allowed, pad, pad, id_str, display_str
HEADER_STRUCT = "<4sIII" "ff" "f" "fff" "ff" "I" "BBBB" "II"
FORMAT_VERSION = 1
HEADER_BYTES = 64
SECTION_ENTRY_BYTES = 16

# collision / nav 문자열 -> 정수.
#
# 빌드할 때 여기서 걸러지므로 JSON 에 오타가 있으면 **서버가 아니라 빌드가**
# 실패한다. 런타임에 조용히 none 으로 떨어지는 것보다 훨씬 낫다.
COLLISION = {"none": 0, "static_walkable": 1, "static_blocker": 2}
NAV = {"include": 0, "exclude": 1, "area:water": 2}

# 배치 한 칸의 바이트 수. C++ / C# 리더도 이 값을 그대로 쓴다.
#   u32 asset | f32 pos[3] | f32 yaw | f32 scale | u8 collision, nav, flags, pad
# 손으로 24 라고 적었다가 --verify 가 "배치 2002 에셋 불일치"로 잡았다.
# 필드를 하나 늘리면 여기도 같이 고쳐야 한다 — 그래서 상수로 둔다.
PLACEMENT_STRIDE = 28
PLACEMENT_STRUCT = "<I" "fff" "ff" "BBBB"

# 배치 플래그 비트
PLACEMENT_SMOKE = 1 << 0


def fnv1a(data: bytes) -> int:
    """FNV-1a 32비트. 세 언어에서 같은 값이 나와야 하므로 가장 단순한 것을 쓴다."""
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


class StringTable:
    """모든 문자열을 한 곳에 모으고 나머지는 인덱스로 참조한다.

    배치 2000개가 각자 "prop_tree_pine_01" 을 들고 있으면 바이너리가 JSON 만큼
    커진다. 표로 빼면 에셋 ID 는 90여 개뿐이라 배치 한 칸이 4바이트로 줄어든다.
    """

    def __init__(self) -> None:
        self._items: list[str] = []
        self._index: dict[str, int] = {}

    def add(self, text: str | None) -> int:
        text = text or ""
        if text not in self._index:
            self._index[text] = len(self._items)
            self._items.append(text)
        return self._index[text]

    def encode(self) -> tuple[bytes, int]:
        """(본문, 문자열 수). 각 항목은 u32 길이 + UTF-8 바이트 + 4바이트 패딩."""
        out = bytearray()
        for text in self._items:
            raw = text.encode("utf-8")
            out += struct.pack("<I", len(raw))
            out += raw
            pad = (-len(raw)) % 4
            out += b"\0" * pad
        return bytes(out), len(self._items)


class Writer:
    """섹션을 모았다가 마지막에 한 번에 쓴다.

    오프셋을 미리 계산하지 않는 이유는, 섹션을 하나 추가할 때마다 손으로 더하는
    코드가 되면 거기서 반드시 틀리기 때문이다. 다 모은 뒤 기계적으로 배치한다.
    """

    def __init__(self) -> None:
        self._sections: list[tuple[bytes, bytes, int]] = []

    def add(self, tag: str, body: bytes, count: int) -> None:
        assert len(tag) == 4, tag
        assert len(body) % 4 == 0, f"{tag} 본문이 4바이트 정렬이 아니다: {len(body)}"
        self._sections.append((tag.encode("ascii"), body, count))

    def build(self, header_fields: dict) -> bytes:
        table_bytes = SECTION_ENTRY_BYTES * len(self._sections)
        offset = HEADER_BYTES + table_bytes

        table = bytearray()
        bodies = bytearray()
        for tag, body, count in self._sections:
            table += struct.pack("<4sIII", tag, offset, len(body), count)
            bodies += body
            offset += len(body)

        header = struct.pack(
            HEADER_STRUCT,
            MAGIC,
            FORMAT_VERSION,
            len(self._sections),
            header_fields["terrain_hash"],
            header_fields["size_x"], header_fields["size_z"],
            header_fields["sector_size"],
            *header_fields["spawn"],
            header_fields["y_min"], header_fields["y_max"],
            header_fields["seed"],
            1 if header_fields["pvp"] else 0,
            1 if header_fields["spawn_allowed"] else 0, 0, 0,
            header_fields["id_str"], header_fields["display_str"],
        )
        assert len(header) == HEADER_BYTES, len(header)
        return bytes(header) + bytes(table) + bytes(bodies)


def pack_floats(values: list[float]) -> bytes:
    return struct.pack(f"<{len(values)}f", *values)


def read_sections(raw: bytes) -> tuple[dict, dict[str, tuple[int, int, int]]]:
    """기록기와 같은 파일을 되읽어 검증할 때 쓴다. (헤더, {태그: (offset, bytes, count)})"""
    if raw[:4] != MAGIC:
        raise ValueError(f"magic 이 다르다: {raw[:4]!r}")

    fields = struct.unpack_from(HEADER_STRUCT, raw, 0)
    header = {
        "version": fields[1],
        "section_count": fields[2],
        "terrain_hash": fields[3],
        "size_x": fields[4], "size_z": fields[5],
        "sector_size": fields[6],
        "spawn": (fields[7], fields[8], fields[9]),
        "y_min": fields[10], "y_max": fields[11],
        "seed": fields[12],
        "pvp": bool(fields[13]), "spawn_allowed": bool(fields[14]),
        "id_str": fields[17], "display_str": fields[18],
    }

    sections: dict[str, tuple[int, int, int]] = {}
    for i in range(header["section_count"]):
        tag, offset, size, count = struct.unpack_from(
            "<4sIII", raw, HEADER_BYTES + i * SECTION_ENTRY_BYTES)
        sections[tag.decode("ascii")] = (offset, size, count)
    return header, sections


def decode_strings(raw: bytes, offset: int, count: int) -> list[str]:
    out: list[str] = []
    cursor = offset
    for _ in range(count):
        (length,) = struct.unpack_from("<I", raw, cursor)
        cursor += 4
        out.append(raw[cursor:cursor + length].decode("utf-8"))
        cursor += length + ((-length) % 4)
    return out


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
