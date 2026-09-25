#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# 샌드박스(리눅스)에서 GameServer를 MinGW-w64로 교차 빌드한다 → Wine으로 실행.
#
#   tools/sandbox/build_server.sh [빌드 폴더(기본 build/sandbox-server)]
#   wine <빌드 폴더>/GameServer.exe [--view-range 9]
#
# · 소스는 건드리지 않는다: 빌드 폴더로 복사한 뒤 세 가지만 맞춘다.
#     1) Windows식 include 경로(`#include "Core\X.h"`) → `/`
#     2) steady_clock → sandbox_clock(아래 · SandboxClock.h)
#     3) 대소문자만 다른 헤더 이름(ContentiD.h ↔ ContentID.h) → 얇은 래퍼 헤더
# · DB는 인메모리 대체물(tools/sandbox/MemDBConnection.cpp)로 바꿔 링크한다.
# · 시계는 샌드박스 시계(SandboxClock.h)로 바꾼다 — HARVEN_CLOCK=/dev/shm/… 이면 녹화 클라이언트와
#   한 시계로 움직이고(느린 소프트웨어 렌더링에서도 영상의 시간 간격이 실제와 같다), 없으면 실제 시계.
#   MSSQL · ODBC 동작 확인은 실기에서.
# · 필요한 패키지: mingw-w64(g++-posix), wine64
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build/sandbox-server}"
SRC="$OUT/src"
CXX="${CXX:-x86_64-w64-mingw32-g++-posix}"
JOBS="${JOBS:-$(nproc)}"

rm -rf "$SRC"
mkdir -p "$SRC" "$OUT/obj"
cp -r "$ROOT/GameServer/." "$SRC/"
rm -rf "$SRC/x64" "$SRC/Binary"

# 1) 역슬래시 include → 슬래시
grep -rl --include=*.h --include=*.cpp '#include "[^"]*\\' "$SRC" | while read -r f; do
	sed -i -E ':a; s/(#include "[^"\\]*)\\/\1\//; ta' "$f"
done
# BOM이 붙은 파일(﻿#pragma once)도 gcc는 읽는다. CRLF도 그대로 둔다.

# 2) 샌드박스 시계: steady_clock → sandbox_clock(HARVEN_CLOCK 공유 시계, 없으면 실제 시계). SandboxClock.h 참고
grep -rl --include=*.h --include=*.cpp 'steady_clock' "$SRC" | while read -r f; do
	sed -i -E 's/std::chrono::steady_clock/sandbox_clock/g; s/chrono::steady_clock/sandbox_clock/g' "$f"
done

# 3) 대소문자 래퍼
mkdir -p "$SRC/_case"
echo '#pragma once' > "$SRC/_case/ContentID.h"
echo '#include "../ObjID/ContentiD.h"' >> "$SRC/_case/ContentID.h"

INC=(-I"$SRC" -I"$SRC/_case")
for d in Core Area GameObject Monster Network ObjID Route Stat Subject Thread Transform User Item Zone Content Combat Npc Quest Data; do
	[ -d "$SRC/$d" ] && INC+=(-I"$SRC/$d")
done

# vcxproj에 등록된 .cpp 목록(DBConnection.cpp는 인메모리 대체물로)
mapfile -t CPPS < <(grep -o 'ClCompile Include="[^"]*\.cpp"' "$ROOT/GameServer/GameServer.vcxproj" | sed -E 's/ClCompile Include="([^"]*)"/\1/' | tr '\\' '/')
FLAGS=(-std=c++20 -O2 -g0 -DWIN32 -DNDEBUG -D_CONSOLE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00
       -finput-charset=UTF-8 -fexec-charset=UTF-8 -w -pthread -include "$ROOT/tools/sandbox/SandboxClock.h")

objs=()

pids=()
for c in "${CPPS[@]}"; do
	src="$SRC/$c"
	if [ "$c" = "Core/DBConnection.cpp" ]; then src="$ROOT/tools/sandbox/MemDBConnection.cpp"; fi
	obj="$OUT/obj/$(echo "$c" | tr '/' '_' | sed 's/\.cpp$/.o/')"
	objs+=("$obj")
	( "$CXX" "${FLAGS[@]}" "${INC[@]}" -c "$src" -o "$obj" || { echo "FAILED: $c"; exit 1; } ) &
	pids+=($!)
	if [ "${#pids[@]}" -ge "$JOBS" ]; then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
done
for p in "${pids[@]}"; do wait "$p"; done
"$CXX" "${FLAGS[@]}" -c "$ROOT/tools/sandbox/SandboxClock.cpp" -o "$OUT/obj/SandboxClock.o"
objs+=("$OUT/obj/SandboxClock.o")

"$CXX" -o "$OUT/GameServer.exe" "${objs[@]}" -static -static-libgcc -static-libstdc++ -pthread \
	-lws2_32 -lmswsock -lodbc32 -lpsapi -ldbghelp
echo "OK: $OUT/GameServer.exe"
