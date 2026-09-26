#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Phase 3-B 채팅 시험 — 클라이언트 세 개(c3a · c3b · c3c)를 같은 마을에 띄워 채널 채팅을 주고받고 판정한다.
#
#   tools/sandbox/chat3.sh <클라 실행 폴더> <출력 폴더> [record]
#
#   a: 일반 · 전체 · (10초 안에 다시 전체 → 막힘) · 거래 · 귓속말(→ b) · 아이템 고리 · 도배 7줄(5줄만) · 같은 말 세 번(셋째 막힘)
#   b: 귓속말을 받으면 /r로 답 · 일반    c: 일반(같은 지역에서 모두 보인다)
#   서버는 먼저 띄워 둔다(run.sh server — record면 --clock 공유 시계). 판정은 chat3_check.py.
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CLI="$1"; OUT="$2"; REC="${3:-}"
mkdir -p "$OUT"
cd "$CLI"
extra=()
[ -f /dev/shm/harven_clock ] && extra=(--sandbox-clock /dev/shm/harven_clock)
for r in a b c; do
	rm -f "config/ui_c3$r.json"
	recargs=()
	if [ "$REC" = "record" ]; then recargs=(--record "$OUT/rec_$r" --record-after 3 --seconds 24 --fps 15 --fixed-step); fi
	DISPLAY=:99 timeout 400 ./Client2D --mute --no-msaa "${extra[@]}" --name "c3$r" --size 1280x720 --hour 12 --script "chat3:$r" \
		--screenshot "c3$r.png" --after 27 "${recargs[@]}" > "$OUT/c3$r.log" 2>&1 &
	sleep 0.7
done
wait
for r in a b c; do [ -f "c3$r.png" ] && mv "c3$r.png" "$OUT/"; done
python3 "$ROOT/tools/sandbox/chat3_check.py" "$OUT"
