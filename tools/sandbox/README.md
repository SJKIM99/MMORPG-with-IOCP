# tools/sandbox — 리눅스 샌드박스에서 서버 · 클라이언트 · 봇 돌리기

GPU도 MSSQL도 없는 리눅스(예: Claude 작업 공간)에서 **같은 소스**로 전체를 띄워 검증 · 녹화하기 위한 도구다.
MSVC · 실제 MSSQL · 실제 GPU 확인은 여기서 대신할 수 없다 — 결과 보고서의 "실기 확인 필요"에 남긴다.

| 파일 | 하는 일 |
|---|---|
| `build_server.sh` | GameServer를 MinGW-w64로 교차 빌드(→ Wine). 소스는 복사본만 고친다: `\` include · 대소문자 헤더 · 시계 |
| `MemDBConnection.cpp` | `Core/DBConnection.cpp` 대신 링크하는 **인메모리 DB**. `Database/setup.sql` 프로시저를 같은 의미로(프로시저 하나 = 락 한 번 = 트랜잭션 하나) |
| `SandboxClock.h/.cpp` | 서버의 `steady_clock`을 바꿔 넣는 시계. `HARVEN_CLOCK=/dev/shm/harven_clock`이면 공유 시계, 없으면 실제 시계 |
| `bot/HarvenBot.cpp` · `build_bot.sh` | 확장 세션으로 붙어 정해진 길(원 · 왕복)을 규칙 속도로 걷는 봇 — "다른 플레이어" 시험용 |
| `run.sh` | 서버 · 봇 · 클라이언트(Xvfb) 띄우기/내리기, 영상 → GIF |
| `motion_sim.cpp` · `build_motion_sim.sh` | 게임 · 서버 없이 `game/Motion`과 걸음 계획을 30fps로 돌려 이동 기록(csv)을 만드는 모의 시험(아래) |
| `regress.py` | 모든 `--script` 시나리오를 차례로 돌려 스크린샷 · 종료 코드 · 성능 줄을 모은다(회귀 검사) |
| `input_test.py` | Xvfb 창에 xdotool로 **실제** 키보드 · 마우스를 넣는 시험(조작 설정 · 채팅 중 차단 · 창 단축키) |

## 한 번 준비

```bash
sudo apt-get install -y mingw-w64 wine64 xvfb x11-utils ffmpeg libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git build/raylib-5.5   # 아카이브 URL이 막힌 곳에서
cmake -S Client2D -B build/client -G Ninja -DCMAKE_BUILD_TYPE=Release -DRAYLIB_SOURCE_DIR=$PWD/build/raylib-5.5
cmake --build build/client
tools/sandbox/build_server.sh build/server
tools/sandbox/build_bot.sh build/harven_bot
```

## 보통 실행(실제 시계)

```bash
tools/sandbox/run.sh server
tools/sandbox/run.sh client --name test1 --class 0 --size 1600x900 --script plaza --screenshot shot.png --after 8
tools/sandbox/run.sh stop
```

## 녹화 — 공유 시계(lockstep)

소프트웨어 렌더링(llvmpipe)은 1600×900 한 장에 약 0.2초가 걸린다(대부분 바닥 셰이더). 실시간으로 뜨면 5fps 영상이 된다.
그래서 녹화할 때는 **세상 전체를 한 시계로 묶는다**:

- 클라이언트(`--sandbox-clock`)가 프레임마다 공유 시계를 정확히 1/fps초씩 올린다.
- 서버(`HARVEN_CLOCK`)의 모든 시간(`GetNowTime` · 타이머 · 토큰 버킷 · 몬스터 · NPC)과 봇이 그 시계를 읽는다.
- 그리기가 아무리 느려도 영상 한 장 = 게임 시간 1/fps초 — 30fps 영상이 실제 게임과 같은 간격으로 움직인다.
  `--netsim` 지연도 같은 시계로 잰다.

```bash
tools/sandbox/run.sh server --clock
tools/sandbox/run.sh bot --name bot1 --class 2 --circle t21.5,t21.5,5 --start 1      # 분수 둘레를 도는 봇
tools/sandbox/run.sh client --name rec1 --class 0 --size 1600x900 --f3 --script follow:bot1 \
    --record out/follow --seconds 10 --fps 30 --record-after 3 [--netsim latency=150,jitter=40,spike=2]
tools/sandbox/run.sh gif out/follow/capture.mp4 out/follow.gif 720
tools/sandbox/run.sh stop
```

녹화 시나리오(`Client2D --script`, 사람과 같은 마우스 경로 — `game/Input.h`):
`runline[:dx,dy[,초[,x,y]]]` 화면 방향으로 누른 채 달리기(x,y = 먼저 걸어가 설 마을 기준 칸 — 부활 칸이 무작위라서) ·
`runcircle[:초]` 분수 둘레 원 그리기 · `follow:이름` 그 플레이어 따라가기. 조명을 맞추려면 `--hour 12`(게임 시각은 실제 시계 24분 = 하루).

분석 · 비교: `tools/record/analyze_trace.py stats out.csv [--remote]`(구간별 속도 · 멈칫 · 방향 깜빡임 · 원격 끊김),
`analyze_trace.py plot 그래프.png 전=a.csv 후=b.csv [--remote]`, `tools/record/compare.sh`(전/후 영상 나란히).

## 모의 시험 — motion_sim

녹화 한 번은 몇 분 걸린다. 조율은 먼저 모의 시험으로 한다(같은 `game/Motion.cpp` · `game/Path.cpp`, 서버 토큰 버킷도 흉내):

```bash
tools/sandbox/build_motion_sim.sh                       # → build/motion_sim
build/motion_sim ray -26.6 5 --at 14,19 --out r.csv     # 곧게(칸 좌표 각도°, 마을 기준 출발 칸)
build/motion_sim circle 30 --at 22,28 --out c.csv       # 원 그리기(runcircle과 같은 조준)
build/motion_sim circle 30 --latency 150 --jitter 40 --spike 2 --catchup 200   # 서버 되찾기 창 실험(거절 수)
build/motion_sim remote 30 150 40 2 --botpace --bg 4 --bgpoisson --seed 3      # 원격 재생(봇 박자 · 들쭉날쭉한 배경 패킷)
python3 tools/record/analyze_trace.py stats c.csv
```
`--noguide`: 안내선 없이(따라가기 · 쫓아가기처럼 궤적 앞보기). 마을 밖 들판은 예전 장애물 무늬가 촘촘해 곧은 길이 거의 없다 — `--at`으로 광장 · 길을 쓴다.
