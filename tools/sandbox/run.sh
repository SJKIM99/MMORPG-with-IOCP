#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# 샌드박스 실행 도우미 — 서버(Wine) · 봇 · 클라이언트(Xvfb)를 띄우고 내린다.
#
#   run.sh server [--clock]      서버 시작(--clock: 공유 시계 /dev/shm/harven_clock — 녹화용)
#   run.sh stop                  서버 · 봇 모두 내림
#   run.sh bot <봇 인자…>         봇 하나를 뒤에서(공유 시계가 있으면 자동으로 씀)
#   run.sh client <클라 인자…>    클라이언트(Xvfb :99, 소프트웨어 렌더링). 공유 시계가 있으면 --sandbox-clock
#   run.sh gif <mp4> <gif> [폭] [시작초] [길이초] [가운데 비율] [fps]   영상 → GIF(팔레트 2패스)
#
# 환경 변수: BUILD(기본 저장소/build) — server/GameServer.exe · client/Client2D · harven_bot 위치
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
CLOCK=/dev/shm/harven_clock
export WINEDEBUG=-all
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"

ensure_display() {
	if ! xdpyinfo -display :99 >/dev/null 2>&1; then
		(Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &)
		sleep 1.5
	fi
}

case "${1:-}" in
server)
	shift
	"$0" stop >/dev/null 2>&1
	if [ "${1:-}" = "--clock" ]; then
		python3 -c "import struct; open('$CLOCK','wb').write(struct.pack('<qq', 10_000_000_000, 0))"
		export HARVEN_CLOCK=$CLOCK
	else
		rm -f "$CLOCK"
		unset HARVEN_CLOCK
	fi
	cd "$BUILD/server" && (nohup wine GameServer.exe > "$BUILD/server/server.log" 2>&1 &)
	for i in $(seq 1 60); do
		grep -q "DB Connect Success" "$BUILD/server/server.log" 2>/dev/null && break
		sleep 1
	done
	sleep 2
	echo "server up ($( [ -n "${HARVEN_CLOCK:-}" ] && echo 'shared clock' || echo 'real clock'))"
	;;
stop)
	for p in $(pgrep -f 'harven_bot' ); do kill "$p" 2>/dev/null; done
	for p in $(ps -eo pid,args | awk '/[G]ameServer\.exe/ {print $1}'); do kill "$p" 2>/dev/null; done
	wineserver -k 2>/dev/null
	sleep 1
	;;
bot)
	shift
	extra=()
	[ -f "$CLOCK" ] && extra=(--clock "$CLOCK")
	(nohup "$BUILD/harven_bot" "${extra[@]}" "$@" >> "$BUILD/bot.log" 2>&1 &)
	sleep 0.5
	;;
client)
	shift
	ensure_display
	extra=()
	[ -f "$CLOCK" ] && extra=(--sandbox-clock "$CLOCK")
	cd "$BUILD/client" && DISPLAY=:99 ./Client2D --mute --no-msaa "${extra[@]}" "$@"
	;;
gif)
	# run.sh gif <mp4> <gif> [폭=560] [시작초=0] [길이초] [잘라낼 가운데 비율=0.62] [fps=15]
	in="$2"; out="$3"; w="${4:-560}"; ss="${5:-0}"; t="${6:-}"; crop="${7:-0.62}"; fps="${8:-15}"
	dur=()
	[ -n "$t" ] && dur=(-t "$t")
	pal="$(mktemp --suffix=.png)"
	vf="crop=iw*$crop:ih*$crop,fps=$fps,scale=$w:-1:flags=lanczos"
	ffmpeg -y -loglevel error -ss "$ss" "${dur[@]}" -i "$in" -vf "$vf,palettegen=max_colors=128:stats_mode=diff" "$pal"
	ffmpeg -y -loglevel error -ss "$ss" "${dur[@]}" -i "$in" -i "$pal" -lavfi "$vf[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" "$out"
	rm -f "$pal"
	ls -la "$out"
	;;
*)
	sed -n 2,14p "$0"
	;;
esac
