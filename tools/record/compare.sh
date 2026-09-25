#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# 전/후 녹화를 나란히 — 가운데를 잘라(카메라가 캐릭터를 따라가므로 캐릭터가 가운데) 이름표를 붙인다.
#
#   compare.sh <전.mp4> <후.mp4> <out.mp4> <전 시작초> <후 시작초> <길이초> "<전 이름표>" "<후 이름표>" [자르기 비율=0.5] [한쪽 폭=480]
#   GIF: tools/sandbox/run.sh gif out.mp4 out.gif 640 0 6 1 15
#
# 시작초는 두 영상에서 같은 순간(예: 달리기 시작 0.3초 전)이 되게 맞춘다 — 녹화 시각이 서로 다르면
# --trace-move 기록의 첫 움직임 시각과 --record-after로 계산한다.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
B="$1"; A="$2"; OUT="$3"; SB="$4"; SA="$5"; D="$6"; LB="$7"; LA="$8"; C="${9:-0.5}"; W="${10:-480}"
FONT="$ROOT/Client2D/assets/fonts/Pretendard-SemiBold.ttf"
lab() { echo "drawtext=fontfile=$FONT:text='$1':x=12:y=10:fontsize=20:fontcolor=0xEDE7DB:box=1:boxcolor=0x111519@0.72:boxborderw=6"; }
ffmpeg -y -loglevel error -ss "$SB" -t "$D" -i "$B" -ss "$SA" -t "$D" -i "$A" -filter_complex \
	"[0:v]crop=iw*$C:ih*$C,scale=$W:-2:flags=lanczos,$(lab "$LB")[a];[1:v]crop=iw*$C:ih*$C,scale=$W:-2:flags=lanczos,$(lab "$LA")[b];[a][b]hstack=inputs=2,format=yuv420p" \
	-c:v libx264 -crf 20 -preset slow -r 30 -an "$OUT"
ls -la "$OUT"
