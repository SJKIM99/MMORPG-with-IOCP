#!/usr/bin/env bash
# 시험용 봇(tools/sandbox/bot) 빌드 — 리눅스. 서버 헤더 + Client2D의 net 계층을 그대로 쓴다.
#   tools/sandbox/build_bot.sh [출력 파일(기본 build/harven_bot)]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build/harven_bot}"
SHIM="$(dirname "$OUT")/bot_case_shim"
mkdir -p "$SHIM"
printf '#pragma once\n#include "%s/GameServer/ObjID/ContentiD.h"\n' "$ROOT" > "$SHIM/ContentID.h"
g++ -std=c++20 -O2 -w -I"$SHIM" -I"$ROOT/Client2D/src" -I"$ROOT/GameServer" -I"$ROOT/GameServer/ObjID" -I"$ROOT/GameServer/Item" \
	"$ROOT/tools/sandbox/bot/HarvenBot.cpp" "$ROOT/Client2D/src/net/NetClient.cpp" "$ROOT/Client2D/src/net/Socket.cpp" \
	-o "$OUT" -pthread
echo "OK: $OUT"
