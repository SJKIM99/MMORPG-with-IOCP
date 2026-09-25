# assetgen — 하르벤의 그림 · 목소리를 Higgsfield로 만드는 도구

`Client2D/assets/`의 그림(`sprites` · `terrain` · `icons` · `fx` · `ui`)과 대사(`voice`)는 모두 여기서 만들었다.
스타일 규칙은 [docs/DESIGN_SYSTEM.md 9장](../../docs/DESIGN_SYSTEM.md#9-게임-그림--higgsfield).

```
catalog.py ──emit──▶ Higgsfield generate_image_batch (8개씩)
    ▲                        │ job id
    │                        ▼
 스타일 · 주문서        batch.py register ─▶ jobs.json
                             │ jobs_wait 결과
                             ▼
                    ingest.sh (URL 기록 + 내려받기) ─▶ 원본 PNG (저장소 밖)
                             │
                             ▼
                    process.py ─▶ Client2D/assets/{sprites,terrain,icons,fx,ui}
                    review.py  ─▶ 검토용 한 장(발 위치 · 기준 높이)

voices.py emit ─▶ Higgsfield generate_audio_batch(text2speech_v2) ─▶ voices.py register · urls · fetch · process ─▶ assets/voice
```

## 파일

| 파일 | 하는 일 |
|---|---|
| `catalog.py` | 주문서 — 118개 요청(플레이어 동작판 8 · 몬스터 34 · NPC 13 · 동물 3 · 바닥 20 · 소품 10 · 벽 · 지붕 6 · 아이콘 11 · 효과 10 · UI 3). 종류마다 스타일 문구(`STYLE_*`)와 2x2 동작판 배치(`LAYOUT_4`)를 붙인다. |
| `batch.py` | `emit A B` = A~B-1번 요청을 `generate_image_batch` 입력 JSON으로(이미 job이 있으면 뺀다) · `register '<jobs>'` · `urls '<jobs_wait 결과>'` · `status` · `waitlist` |
| `jobs.json` | 요청 이름 → job id · 결과 URL. 다시 뽑은 것은 `replaced`에 예전 id를 남긴다. |
| `ingest.sh` | `urls` + `fetch`(+ 다음 묶음 `emit`)를 한 번에. 원본 폴더는 `ASSETGEN_RAW`(기본 `~/.cache/harven-assetgen/raw`). |
| `fetch.py` | jobs.json의 URL을 원본 폴더로 내려받는다(있으면 건너뜀). |
| `process.py` | 원본 → 게임 아틀라스 · 텍스처(아래). |
| `review.py` | 아틀라스 검토 — 스프라이트마다 발 위치(빨간 점)와 기준 높이(노란 선). `python3 review.py <sprites 폴더> actors out.jpg mon_` |
| `voices.py` | 대사 35줄 — 배역(목소리 id) · 문장 · 반음 · 울림. `emit` · `register` · `urls` · `fetch` · `process` |
| `voice_jobs.json` | 대사 job id · URL |

## 순서

```bash
cd tools/assetgen
python3 batch.py status                 # 남은 것
python3 batch.py emit 0 8               # → generate_image_batch 에 그대로 (한 번에 8개 이하)
python3 batch.py register '<jobs>'      # 돌려받은 jobs 배열
./ingest.sh '<jobs_wait 결과>' 8 16     # URL 기록 · 내려받기 · 다음 묶음 JSON
python3 process.py ~/.cache/harven-assetgen/raw ../../Client2D/assets            # 전부
python3 process.py ~/.cache/harven-assetgen/raw ../../Client2D/assets fx ui      # 일부만
```

- 모델: `gpt_image_2_5` · `quality: medium` · `1k` · `1:1`(장당 0.5 크레딧). 스프라이트 · 소품 · 아이콘 · UI 조각은 `background: transparent`, 바닥 · 벽 · 효과(검은 바탕)는 `opaque`.
- 플레이어 동작판은 직업 기본 그림, NPC는 대화창 초상화를 `medias: [{role: "image_references", value: <job id>}]`로 넘긴다.
- 한 장만 다시 뽑으려면 jobs.json에서 그 이름의 `job` · `url`을 지우고(예전 id는 `replaced`로) `emit`한다. 원본 폴더의 같은 이름 PNG도 지운다.

## process.py가 하는 일

| 대상 | 가공 |
|---|---|
| 동작판(`actors`) | 2x2 칸 나누기(알파 성분 + 골짜기) → 잡티 제거 → **몸 중심 맞춤 발 기준점**(알파를 깎은 몸통의 무게중심, 서기 자세의 중심 ↔ 발 거리를 다른 자세에 적용) → 캐릭터마다 같은 배율 → 아틀라스(2048, 여백 6px, 가장자리 색 번짐) → `sprites/actors_N.png` + `actors.txt` |
| 소품(`props`) | 칸 나누기 → 종류별 기준 높이로 맞춤 → 발(밑동) 기준점 → `sprites/props_N.png` + `props.txt` |
| 아이콘(`icons`) | 3x3 아이콘판을 칸마다 나눠 128px 가운데 맞춤 → `icons/icons.png`(16열) + `icons.txt` |
| 바닥(`terrain`) | 이음매 없애기(아래) → 512px → 5x4 아틀라스(칸 576 = 512 + 가장자리 감쌈 32 x 2) → `terrain/ground_atlas.jpg` + `.txt` |
| 벽 · 지붕(`walls`) | 이음매 없애기(빛 평탄화 없이) → `terrain/<이름>.jpg` |
| 효과(`fx`) | 검은 바탕은 밝기 → 알파(더하기 혼합용), 연기는 알파 그대로 → `fx/<이름>.png` |
| UI(`ui`) | 창 바탕 텍스처(이음매 없음) · 모서리 · 구분선 · 명판 · 고리 · 커서 4 → `ui/<이름>.png` |

**이음매 없애기**(`seamless_tex`): 가장자리 대비를 재서(`seam_ratio`) 이음매가 크면 자기상관으로 반복 주기를 찾아 잘라 내고(`periodic_crop`), 좁은 띠만 섞는다(`crossfade_axis`). 바닥은 넓은 빛 얼룩을 평탄화한다(`flatten_light`). 넓게 섞으면 돌벽 · 판자가 두 겹으로 비치므로 띠는 좁게.

**목록 한 줄**(`actors.txt` · `props.txt`): `이름 아틀라스번호 x y w h 발x 발y 기준높이` — 게임은 발을 바닥에 놓고, `월드 높이 / 기준높이`로 모든 동작을 같은 배율로 그린다.

## 목소리(voices.py)

```bash
python3 voices.py emit                  # generate_audio_batch 입력(8개씩): text2speech_v2 · variant elevenlabs · voice_type preset
python3 voices.py register '<jobs>'
python3 voices.py urls '<jobs_wait 결과>'
python3 voices.py fetch ~/.cache/harven-assetgen/voice
python3 voices.py process ~/.cache/harven-assetgen/voice ../../Client2D/assets   # ffmpeg 필요
```

- 가공: 앞뒤 침묵 자르기 → 반음 올림/내림(`asetrate`) → 울림(`hall` 큰 방 · `boss` 깊은 울림 + 낮은 겹 · `demon` 거친 울림) → 소리 크기 맞춤(`loudnorm`) → ogg vorbis 모노 22kHz. `voice/voice.txt`에 열쇠 목록.
- 열쇠 = 게임이 찾는 이름: `npc_<NPC id>`(대화창을 열 때) · `pc_<직업>_ult|lvl|die` · `boss_<몬스터 id>_spawn|die`.
- 장당 0.15 크레딧. 목소리는 미리 듣기 음성의 기본 음높이(F0)를 재서 배역에 맞췄다.

## 한계

- **효과음 · 음악은 만들 수 없다.** Higgsfield의 효과음 · 음악 모델(sonilo · mirelo 등)은 Higgsfield 게임 제작 기능 안에서만 쓰게 막혀 있다. 타격음 · 휘두름 · 처치음 등 모든 효과음은 `Client2D/src/audio/Sfx.cpp`가 코드로 합성한다.
- 한 번에 8개 넘게 넣지 않는다(동시 실행 제한). 음성은 짧은 시간에 많이 넣으면 429(rate limit) — 조금 뒤 다시 넣는다.
