#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# STRESS_TEST(Windows 봇 부하 도구)를 머리 없이(그리기 없이) MinGW로 교차 빌드 → Wine으로 실행.
#
#   tools/sandbox/build_stress.sh [빌드 폴더(기본 build/stress)] [최대 봇 수(기본 20000)]
#   wine <빌드 폴더>/stress.exe <초> [csv]
#
# · 소스는 건드리지 않는다: 복사본에서 Protocol.h 절대경로 include만 바꾸고, 최대 봇 수(MAX_TEST)를 줄인다
#   (원본 50만은 정적 배열이 수백 MB — 샌드박스 메모리에 맞춘다. 봇 행동 · 증가 규칙은 그대로).
# · 서버 변경 Phase의 전/후 비교용 — 같은 샌드박스에서 같은 명령으로 두 번 재고 비율을 본다.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build/stress}"
MAXTEST="${2:-20000}"
CXX="${CXX:-x86_64-w64-mingw32-g++-posix}"
mkdir -p "$OUT/src"
cp "$ROOT/STRESS_TEST/STRESS_TEST/NetworkModule.cpp" "$ROOT/STRESS_TEST/STRESS_TEST/NetworkModule.h" "$ROOT/STRESS_TEST/STRESS_TEST/Types.h" "$OUT/src/" 2>/dev/null || true
cp "$ROOT/tools/sandbox/stress/stress_main.cpp" "$OUT/src/"
sed -i -E 's|#include "C:.*Protocol\.h"|#include "Protocol.h"|' "$OUT/src/NetworkModule.cpp"
sed -i -E "s/const static int MAX_TEST *= *[0-9]+;/const static int MAX_TEST        = $MAXTEST;/" "$OUT/src/NetworkModule.cpp"
grep -q '#include "Protocol.h"' "$OUT/src/NetworkModule.cpp" || { echo "Protocol.h include 치환 실패"; exit 1; }
# 대소문자만 다른 Windows 헤더 이름(WinSock2.h · Windows.h) → 얇은 래퍼
mkdir -p "$OUT/case"
printf '#pragma once\n#include <winsock2.h>\n' > "$OUT/case/WinSock2.h"
printf '#pragma once\n#include <windows.h>\n' > "$OUT/case/Windows.h"
printf '#pragma once\n#include "ContentiD.h"\n' > "$OUT/case/ContentID.h"
# 서버 Protocol.h(+ 그 안의 상대 include)를 그대로 쓰도록 include 경로로 넘긴다
"$CXX" -O2 -std=c++20 -DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE -I"$OUT/case" -I"$ROOT/GameServer" -I"$ROOT/GameServer/ObjID" -I"$OUT/src" \
	"$OUT/src/NetworkModule.cpp" "$OUT/src/stress_main.cpp" -o "$OUT/stress.exe" -static -lws2_32 -lmswsock -luser32
echo "OK: $OUT/stress.exe (MAX_TEST=$MAXTEST)"
