#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Phase 5 시험 — 결사 · 경매장. 시험마다 서버를 새로 띄운다(인메모리 DB가 비어 있게 — crash만 일지를 이어 쓴다).
#
#   tools/sandbox/phase5.sh <클라 폴더> <서버 빌드 폴더> <출력 폴더> <시험…>
#     guild3        클라 셋: 세우기 · 초대(거절 뒤 수락) · 같은 순번 두 번 · 결사 채팅 · 직위 · 추방 →
#                   추방된 c가 금칙어 이름(거절) 뒤 「서리매」를 세움 → 깃발 뜰 순위(새벽서약 · 서리매)   → [g3] 줄
#     guild3rec     같은 시험을 공유 시계로 녹화(a가 시계를 올리고 녹화, b · c는 따라가기)
#     auction       판매자(lv20_ak_s) 셋 올림 → 구매자(lv20_ab) 철검 구매 → 둘 다 정산함 받기            → [auc] 줄
#     auctionrec    같은 시험 녹화(구매자 화면 · 판매자 화면 두 번) — auctionrecb · auctionrecs는 한쪽만
#     race          한 목록을 두 사람이 같은 순간에 산다 — 한 명만 · 진 쪽 그로시 그대로
#     dup           같은 순번의 구매 요청 두 번 — 두 번째는 Duplicate, 한 번만 팔린다
#     expire        한 시간 = 2초(HARVEN_AUCTION_HOUR_SEC) — 24시간 목록이 48초 뒤 만료 → 정산함
#     crash         일지(HARVEN_MEMDB_JOURNAL) + 구매 트랜잭션 안 멈춤 → 그 사이 서버를 죽이고 다시 켠다 → 총량 비교
#   판정: tools/sandbox/phase5_check.py <출력 폴더>
# 환경: BUILD(기본 저장소/build) — run.sh와 같다(서버는 BUILD/server에 복사해 띄운다)
#       P5_PORT=9100 이면 "곁에서" 모드: 다른 시험(회귀 등)의 서버를 건드리지 않고 BUILD/server_p5run에 따로 띄워
#       그 포트로만 붙는다(run.sh stop · wineserver -k를 쓰지 않는다 — 내릴 때는 그 서버의 PID만).
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CLI="$(cd "$1" && pwd)"; SRV="$(cd "$2" && pwd)"; OUT="$3"; shift 3
BUILD="${BUILD:-$ROOT/build}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
RUN="$ROOT/tools/sandbox/run.sh"
CLOCK=/dev/shm/harven_clock

P5_PORT="${P5_PORT:-}"
P5DIR="$BUILD/server_p5run"
SLOG="$BUILD/server/server.log"
[ -n "$P5_PORT" ] && SLOG="$P5DIR/server.log"
PORTARGS=()
[ -n "$P5_PORT" ] && PORTARGS=(--port "$P5_PORT")

p5_pid() { ps -eo pid,args | awk -v pat="GameServer.exe --port $P5_PORT" 'index($0, pat) && !/awk/ {print $1}'; }
server_down() {
	if [ -n "$P5_PORT" ]; then
		for p in $(p5_pid); do kill "$p" 2>/dev/null; done
		sleep 1.5
	else
		BUILD="$BUILD" "$RUN" stop >/dev/null 2>&1
	fi
}
server_up() {   # $1 = real|shared — 나머지 설정은 환경 변수(HARVEN_*)로 넘긴다
	server_down
	if [ -n "$P5_PORT" ]; then
		mkdir -p "$P5DIR"
		cp "$SRV/GameServer.exe" "$P5DIR/GameServer.exe"
		if [ "$1" = shared ]; then
			python3 -c "import struct; open('$CLOCK','wb').write(struct.pack('<qq', 10_000_000_000, 0))"
			export HARVEN_CLOCK=$CLOCK
		else
			unset HARVEN_CLOCK
		fi
		( cd "$P5DIR" && WINEDEBUG=-all nohup wine GameServer.exe --port "$P5_PORT" > "$P5DIR/server.log" 2>&1 & )
		unset HARVEN_CLOCK
		for i in $(seq 1 60); do grep -q "DB Connect Success" "$P5DIR/server.log" 2>/dev/null && break; sleep 1; done
		sleep 2
	else
		cp "$SRV/GameServer.exe" "$BUILD/server/GameServer.exe"
		if [ "$1" = shared ]; then BUILD="$BUILD" "$RUN" server --clock >/dev/null; else BUILD="$BUILD" "$RUN" server >/dev/null; fi
		sleep 2
	fi
}
server_log() { cp "$SLOG" "$OUT/$1"; }
server_kill() {   # 갑자기 죽인다(장애 시험) — 일지는 줄마다 flush
	if [ -n "$P5_PORT" ]; then for p in $(p5_pid); do kill -9 "$p" 2>/dev/null; done; else wineserver -k 2>/dev/null; fi
	sleep 1
}

# client <이름> <시나리오> <초> [클라 인자…] — 뒤에서 띄운다
#   녹화하지 않는 클라는 작게 · 낮은 품질로(소프트웨어 렌더링 셋이 2코어를 나눠 쓴다 — 시나리오 시간은 프레임마다 최대 0.05초라
#   프레임이 느리면 시나리오도 느려진다). 녹화하는 클라는 VIEW="--size 1280x720"을 앞에 붙여 부른다
client() {
	local name="$1" script="$2" secs="$3"; shift 3
	local view="${VIEW:---size 960x540 --quality 0}"
	rm -f "$CLI/config/ui_$name.json"
	( cd "$CLI" && DISPLAY=:99 timeout $((secs * 12 + 300)) stdbuf -oL ./Client2D --mute --no-msaa "${PORTARGS[@]}" --name "$name" $view --hour 12 \
		--script "$script" --screenshot "p5_$name.png" --after "$secs" "$@" > "$OUT/$name.log" 2>&1 ; \
		[ -f "$CLI/p5_$name.png" ] && mv "$CLI/p5_$name.png" "$OUT/" ) &
}

for t in "$@"; do
	case "$t" in
	guild3)
		server_up real
		client lv20_ga guild3:a 65; sleep 0.8
		client lv20_gb guild3:b 65; sleep 0.8
		client lv20_gc guild3:c 65
		wait
		server_log guild3_server.log
		;;
	guild3rec)
		server_up shared
		VIEW="--size 1280x720" client lv20_ga guild3:a 95 --sandbox-clock "$CLOCK" --record "$OUT/rec_guild3" --record-after 2 --seconds 88 --fps 15; sleep 0.8
		client lv20_gb guild3:b 95 --sandbox-clock-follow "$CLOCK"; sleep 0.8
		client lv20_gc guild3:c 95 --sandbox-clock-follow "$CLOCK"
		wait
		server_log guild3rec_server.log
		;;
	auction)
		server_up real
		client lv20_ak_s auction:sell 60; sleep 1
		client lv20_ab auction:buy 50
		wait
		server_log auction_server.log
		;;
	auctionrec|auctionrecb|auctionrecs)
		# 한 시간 = 2초(물약 12시간 목록이 24초 뒤 만료 → 정산함으로 돌아오는 것까지 한 영상에). 클라도 이 값을 보고 기다린다
		# auctionrecb = 구매자 화면만 · auctionrecs = 판매자 화면만
		export HARVEN_AUCTION_HOUR_SEC=2
		if [ "$t" != auctionrecs ]; then
			# 구매자 화면: 구매자가 시계를 올리고 녹화, 판매자는 따라가기
			server_up shared
			client lv20_ak_s auction:sell 110 --sandbox-clock-follow "$CLOCK"; sleep 1
			VIEW="--size 1280x720" client lv20_ab auction:buy 80 --sandbox-clock "$CLOCK" --record "$OUT/rec_buyer" --record-after 2 --seconds 72 --fps 15
			wait
			server_log auctionrec_buyer_server.log
		fi
		if [ "$t" != auctionrecb ]; then
			# 판매자 화면
			server_up shared
			VIEW="--size 1280x720" client lv20_ak_s auction:sell 110 --sandbox-clock "$CLOCK" --record "$OUT/rec_seller" --record-after 2 --seconds 100 --fps 15; sleep 1
			client lv20_ab auction:buy 90 --sandbox-clock-follow "$CLOCK"
			wait
			server_log auctionrec_seller_server.log
		fi
		unset HARVEN_AUCTION_HOUR_SEC
		;;
	race)
		# 구매 트랜잭션 안에서 1.5초 멈추게 — 먼저 온 요청이 목록을 잠근 동안 늦은 요청이 들어온다(잠금 경로 = Busy를 실제로 지난다)
		export HARVEN_DB_BUY_DELAY_MS=1500
		server_up real
		unset HARVEN_DB_BUY_DELAY_MS
		client lv20_ak_r auction:sellone 60
		sleep 25
		at=$(( $(date +%s%3N) + 45000 ))
		echo "$at" > "$OUT/race_at.txt"
		export HARVEN_RACE_AT=$at
		client lv20_r1 auction:race 70; sleep 0.5
		client lv20_r2 auction:race 70
		unset HARVEN_RACE_AT
		wait
		server_log race_server.log
		;;
	dup)
		server_up real
		client lv20_ak_d auction:sellone 60; sleep 12
		client lv20_dd auction:dup 70
		wait
		server_log dup_server.log
		;;
	expire)
		export HARVEN_AUCTION_HOUR_SEC=2
		server_up real
		unset HARVEN_AUCTION_HOUR_SEC
		client lv20_ak_e auction:sellone 110
		wait
		server_log expire_server.log
		;;
	crash)
		J="$OUT/crash_journal.log"
		rm -f "$J"
		export HARVEN_MEMDB_JOURNAL="$J"
		# ① 올리기(일지에 남는다) → 서버를 정상적으로 내린다
		server_up real
		client lv20_ak_c auction:sellone 45
		client lv20_cb p5idle 20            # 구매자 계정도 먼저 만들어 둔다(②의 기준 총량에 들어가게)
		wait
		server_log crash_1_register.log
		server_down
		# ② 다시 켠다(구매 트랜잭션 안에서 8초 멈추게) — 켤 때의 [Audit]가 기준
		export HARVEN_DB_BUY_DELAY_MS=8000
		server_up real
		unset HARVEN_DB_BUY_DELAY_MS
		client lv20_cb auction:buyone 60
		for i in $(seq 1 240); do
			grep -q "AuctionBuy listing=" "$SLOG" 2>/dev/null && break
			sleep 0.5
		done
		sleep 1
		server_log crash_2_before_kill.log
		server_kill                                   # 트랜잭션 도중 — 커밋 전
		wait
		# ③ 다시 켠다 — 총량이 ②의 기준과 같아야 하고, 목록은 판매 중으로 돌아와 있어야 한다
		server_up real
		sleep 2
		server_log crash_3_restart.log
		# ④ 같은 사람이 다시 산다(이번엔 멈춤 없이) → 커밋 → 다시 켜서 총량 확인
		client lv20_cb auction:buyone 60
		wait
		server_log crash_4_buy.log
		server_down
		server_up real
		sleep 2
		server_log crash_5_final.log
		unset HARVEN_MEMDB_JOURNAL
		;;
	*)
		echo "모르는 시험: $t"
		;;
	esac
done
server_down
python3 "$ROOT/tools/sandbox/phase5_check.py" "$OUT" | tee "$OUT/check.txt"
