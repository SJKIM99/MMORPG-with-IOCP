#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Phase 6 — 전체 플레이 녹화 · 성능. 시험마다 서버를 새로 띄운다(인메모리 DB가 비어 있게).
#
#   tools/sandbox/phase6.sh <클라 폴더> <서버 빌드 폴더> <출력 폴더> <할 일…>
#     fullplay     공유 시계로 한 편 녹화: lv50_fp(서약기사, 1280×720 · 20fps)가 접속 → 마을 투어 → 훈련장 → 들녘 사냥 →
#                  모로즈 → 채팅 → 결사 → 경매까지. 도우미 lv30_fq(fullplay:helper)가 따라간다(귓속말 · 결사 · 구매).
#                  → rec_fullplay/capture.mp4 · fullplay.log([fp] seg/mark 줄) — 자르기는 tools/record/fullplay_cut.py
#     crowd        분수 광장(1600×900 — 품질 기준 해상도): 봇 30(harven_bot --crowd 30, 한 프로세스) + 마을 NPC 33 → [crowd] · [mem] · crowd.csv · crowd.png(F3 그래프)
#     crowd0       같은 자리 · 봇 없이(비교 기준)
#     fullplaytest 녹화 없이 작은 창 · 실제 시계로 흐름만(스모크 시험)
#     fight        대규모 전투: awaken3(훈련장 — 몬스터 30 · 세 사람 동시 각성기) → [bench] · [mem] · fight.csv · fight_1/2.png
#   그래프: tools/perf/plot_frames.py <출력 폴더>
# 환경: BUILD(기본 저장소/build) — run.sh와 같다. BOT(기본 BUILD/harven_bot)
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CLI="$(cd "$1" && pwd)"; SRV="$(cd "$2" && pwd)"; OUT="$3"; shift 3
BUILD="${BUILD:-$ROOT/build}"
BOT="${BOT:-$BUILD/harven_bot}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
RUN="$ROOT/tools/sandbox/run.sh"
CLOCK=/dev/shm/harven_clock

server_up() {   # $1 = real|shared
	BUILD="$BUILD" "$RUN" stop >/dev/null 2>&1
	cp "$SRV/GameServer.exe" "$BUILD/server/GameServer.exe"
	if [ "$1" = shared ]; then BUILD="$BUILD" "$RUN" server --clock >/dev/null; else BUILD="$BUILD" "$RUN" server >/dev/null; fi
	sleep 2
}
server_down() { BUILD="$BUILD" "$RUN" stop >/dev/null 2>&1; }
server_log() { cp "$BUILD/server/server.log" "$OUT/$1"; }

# client <이름> <시나리오> <스크린샷 초> [클라 인자…] — 뒤에서 띄운다(VIEW로 크기 · 품질)
client() {
	local name="$1" script="$2" secs="$3"; shift 3
	local view="${VIEW:---size 960x540 --quality 0}"
	rm -f "$CLI/config/ui_$name.json"
	( cd "$CLI" && DISPLAY=:99 timeout $((secs * 6 + 600)) stdbuf -oL ./Client2D --mute --no-msaa --name "$name" $view --hour "${HOUR:-12}" \
		--script "$script" --screenshot "p6_$name.png" --after "$secs" "$@" > "$OUT/$name.log" 2>&1 ; \
		for f in "$CLI"/p6_"$name"*.png; do [ -f "$f" ] && mv "$f" "$OUT/"; done ) &
}

for t in "$@"; do
	case "$t" in
	fullplay)
		server_up shared
		# 녹화하는 쪽이 시계를 올린다 — 도우미는 따라가기. 녹화는 로그인 화면부터(스크립트가 끝나면 스크린샷 한 장 뒤 끝)
		VIEW="--size 1280x720" client lv50_fp fullplay 3000 --class 0 --sandbox-clock "$CLOCK" \
			--record "$OUT/rec_fullplay" --record-after 0 --seconds 1500 --fps 20
		sleep 1.5
		client lv30_fq fullplay:helper 3000 --class 1 --sandbox-clock-follow "$CLOCK"
		# 도우미는 본인이 끝나도 남아 있다 — 본인 로그에 [fp] done이 뜨면(또는 본인 클라가 끝나면) 내린다
		while sleep 5; do
			if grep -q "^\[fp\] done" "$OUT/lv50_fp.log" 2>/dev/null; then sleep 8; break; fi
			if ! ps -eo args | grep -q "[C]lient2D --mute --no-msaa --name lv50_fp"; then break; fi
		done
		pkill -f "Client2D --mute --no-msaa --name lv30_fq" 2>/dev/null
		wait
		server_log fullplay_server.log
		;;
	fullplaytest)
		# 녹화 없이 빠르게(작은 창 · 실제 시계) — 흐름만 확인
		server_up real
		client lv50_fp fullplay 3000 --class 0
		sleep 1.5
		client lv30_fq fullplay:helper 3000 --class 1
		while sleep 5; do
			if grep -q "^\[fp\] done" "$OUT/lv50_fp.log" 2>/dev/null; then sleep 3; break; fi
			if ! ps -eo args | grep -q "[C]lient2D --mute --no-msaa --name lv50_fp"; then break; fi
		done
		pkill -f "Client2D --mute --no-msaa --name lv30_fq" 2>/dev/null
		wait
		server_log fullplaytest_server.log
		;;
	crowd|crowd0)
		server_up real
		if [ "$t" = crowd ]; then
			# 분수(749.5, 762.5) 둘레 세 겹(반지름 3.5 · 5.5 · 7.5) — 봇은 0.15초씩 늦게 들어와 걷기 시작한다
			( stdbuf -oL "$BOT" --crowd 30 --name crowd --circle 749.5,762.5,3.5 --start 3 --seconds 1200 > "$OUT/bots.log" 2>&1 & )
			sleep 6
			export HARVEN_CROWD_MIN=28
			VIEW="--size 1600x900" client lv20_cw crowd 400 --perf-csv "$OUT/crowd.csv"
		else
			export HARVEN_CROWD_MIN=0
			VIEW="--size 1600x900" client lv20_c0 crowd 400 --perf-csv "$OUT/crowd0.csv"
		fi
		unset HARVEN_CROWD_MIN
		wait
		pkill -f "harven_bot --crowd" 2>/dev/null
		server_log "${t}_server.log"
		;;
	fight)
		server_up real
		VIEW="--size 1600x900" client lv50_p6aw awaken3 60 --class 0 --perf-csv "$OUT/fight.csv" --count 2 --every 6
		wait
		server_log fight_server.log
		;;
	*)
		echo "모르는 할 일: $t"
		;;
	esac
done
server_down
echo "done → $OUT"
