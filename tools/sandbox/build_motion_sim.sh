#!/usr/bin/env bash
# 이동 모의 시험(motion_sim) 빌드 — Client2D의 game/Motion.cpp · game/Path.cpp를 그대로 쓴다.
#   BUILD(기본 저장소/build)/motion_sim 에 만든다. raylib 헤더(Vector2)만 필요하다(RAYLIB_SRC).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
RAYLIB_SRC="${RAYLIB_SRC:-$BUILD/raylib-5.5/src}"
mkdir -p "$BUILD/motion_sim_shim"
echo "#pragma once
#include \"$ROOT/GameServer/ObjID/ContentiD.h\"" > "$BUILD/motion_sim_shim/ContentID.h"
g++ -std=c++20 -O2 -Wall -o "$BUILD/motion_sim" \
	-I"$BUILD/motion_sim_shim" -I"$ROOT/Client2D/src" -I"$ROOT/GameServer" -I"$ROOT/GameServer/ObjID" -I"$ROOT/GameServer/Item" -I"$RAYLIB_SRC" \
	"$ROOT/tools/sandbox/motion_sim.cpp" "$ROOT/Client2D/src/game/Motion.cpp" "$ROOT/Client2D/src/game/Path.cpp"
echo "built $BUILD/motion_sim"
