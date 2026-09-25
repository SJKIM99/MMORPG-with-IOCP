# -*- coding: utf-8 -*-
"""
하르벤 음성 대사 — Higgsfield 음성 합성(text2speech_v2 · ElevenLabs 엔진)으로 만든다.

  python3 voices.py emit                  # generate_audio_batch에 넣을 요청(8개씩)
  python3 voices.py register '<jobs>'     # 돌려받은 job id 기록(voice_jobs.json)
  python3 voices.py urls '<jobs_wait 결과>'
  python3 voices.py fetch <raw 폴더>      # 내려받기
  python3 voices.py process <raw 폴더> <Client2D/assets 폴더>   # ogg(모노 22kHz) + 음높이 · 울림 가공

· Higgsfield의 효과음 · 음악 모델은 Higgsfield 게임 제작 기능 안에서만 쓰게 막혀 있어 효과음은 만들 수 없다.
  음성(대사)만 여기서 만들고, 타격음 등 효과음은 Client2D/src/audio/Sfx.cpp가 합성한다.
· 목소리는 미리 듣기 음성의 기본 음높이(F0)를 재서 배역에 맞게 골랐다(낮은 목소리 = 대장장이 · 보스).
· 대사는 docs/WORLD.md의 말투 규칙(변경말: 하오체 · 하게체, 아이는 반말)을 따른다.
"""
import json
import os
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
JOBS = os.path.join(HERE, "voice_jobs.json")

VOICE = {
    "Gideon": "1ad38ba4-9cc4-4f2f-9fde-b0fefdf67ae5",     # 가장 낮음(~70Hz) — 보스
    "Caspian": "ef70cc83-3015-4bad-9359-0ea968c43ec0",    # 낮고 곧음 — 서약기사
    "Alistair": "d9d5c263-f84e-4752-97b5-3750fcc6fd2f",   # 늙은 남자 — 촌장
    "Sterling": "dc382508-c8bd-443c-8cb2-46e57b8d2e6f",   # 단단한 — 경비대장 · 세베린
    "Barrett": "d603a8cd-3fe1-55e0-9245-617a2589131e",    # 굵은 — 대장장이 · 우르간
    "Orion": "ed69c516-92d2-4b30-a967-617737a342e5",      # 부드러운 — 음유시인
    "Desmond": "563f728c-e249-5a85-97ab-8461e8c09da6",    # 경비병들(음높이로 나눈다)
    "Knox": "195e386a-cb61-5c1b-a53b-0e2f0669c408",       # 농부
    "Harrison": "573e5163-59b3-4926-aab1-951ef2985f81",   # 밤사냥꾼
    "Julian": "95429266-c0ac-4137-a209-63b8812b0f23",     # 각인술사
    "Arthur": "30fc8796-ceb6-4a66-b3a7-4a145ef7f346",     # 길잡이
    "Tallulah": "f32c8f51-449e-4ddf-bdf7-1527e11df917",   # 할멈
    "Nora": "d081b915-6623-4a44-bacf-80d0f1c90a03",       # 여관 주인
    "Imogen": "3811e986-0891-47cf-a1f5-78a1d62a547a",     # 잡화상
    "Hana": "c25f78a0-714e-42af-8da3-a399cef94968",       # 사제
    "Skye": "1fb253b8-928b-4d29-a349-f242a71eaddf",       # 꼬마 미샤(+3반음)
    "Luna": "375a3398-e3b4-4f91-845d-42181e352899",       # 꼬마 안카(+1반음)
}

# (열쇠, 목소리, 대사, 반음, 울림) — 열쇠 = 게임이 찾는 파일 이름(assets/voice/<열쇠>.ogg)
#   울림: "" 없음 · "hall" 큰 방 · "boss" 깊은 울림(+낮은 겹) · "demon" 거친 울림
LINES = [
    # ── 마을 사람(대화창을 열 때) ── 인사말은 WorldData::kNpcs와 같다
    ("npc_1", "Imogen", "어서 오세요. 물약도 두루마리도 오늘 들어온 거예요.", 0, ""),
    ("npc_2", "Barrett", "좋은 무기가 좋은 사냥을 만들지. 쓰던 물건도 값을 쳐 주겠네.", 0, ""),
    ("npc_3", "Arthur", "어디로 가시겠어요? 옛길은 멀수록 값이 오릅니다.", 0, ""),
    ("npc_4", "Sterling", "성벽 안에서 칼을 뽑는 자는 내가 상대하겠소.", 0, ""),
    ("npc_5", "Desmond", "성 밖은 위험하오. 물약을 챙기시오.", 0, ""),
    ("npc_6", "Alistair", "어서 오게, 젊은이. 요즘은 새벽이 늦어도 너무 늦는다네.", 0, ""),
    ("npc_7", "Hana", "새벽이 함께하기를. 다친 곳이 있으면 기도를 올려 드릴게요.", 0, "hall"),
    ("npc_8", "Nora", "따뜻한 수프 한 그릇 어때요? 삼십 그로시면 속이 든든해요.", 0, ""),
    ("npc_9", "Orion", "서약 기사단의 마지막 노래를 들어 보시겠소?", 0, ""),
    ("npc_10", "Skye", "형도 모험가야? 나도 크면 서약기사 될 거야!", 3, ""),
    ("npc_11", "Luna", "쉿! 지금 미샤랑 숨바꼭질하는 중이야.", 1, ""),
    ("npc_12", "Tallulah", "아이고, 젊은이. 이 늙은이 말 좀 들어 보겠수?", -1, ""),
    ("npc_13", "Knox", "허허, 닭 좀 봐 주시오. 자꾸 도망가서 말이오.", 0, ""),
    ("npc_14", "Desmond", "성문 밖 들녘엔 수액덩이와 그락 약탈꾼이 많소. 처음이라면 거기서 시작하시오.", -1, ""),
    ("npc_15", "Desmond", "짐승 몸이 붉게 달아오르면 강공격이오. 바닥을 보고 피하시오.", 1, ""),
    ("npc_16", "Desmond", "순찰 중이오. 무슨 일이오?", -2, ""),
    # ── 플레이어(직업별): 궁극기 · 레벨 업 · 쓰러짐 ──
    ("pc_0_ult", "Caspian", "서약의 이름으로, 단죄한다!", 0, ""),
    ("pc_0_lvl", "Caspian", "서약은 더 단단해진다.", 0, ""),
    ("pc_0_die", "Caspian", "서약을… 지키지 못했군….", 0, ""),
    ("pc_1_ult", "Julian", "하늘이여, 별을 떨어뜨려라!", 0, ""),
    ("pc_1_lvl", "Julian", "새김이 한층 깊어졌다.", 0, ""),
    ("pc_1_die", "Julian", "글자가… 흐려진다….", 0, ""),
    ("pc_2_ult", "Harrison", "그믐달 아래, 끝이다.", 0, ""),
    ("pc_2_lvl", "Harrison", "밤이 나를 더 날카롭게 한다.", 0, ""),
    ("pc_2_die", "Harrison", "어둠이… 나를 부르는군….", 0, ""),
    # ── 필드 보스: 나타날 때 · 쓰러질 때 ──
    ("boss_29_spawn", "Gideon", "누가… 나의 겨울을 깨우는가.", -2, "boss"),
    ("boss_29_die", "Gideon", "봄이… 오는가….", -2, "boss"),
    ("boss_30_spawn", "Barrett", "그락의 도끼가 너를 기다린다!", -3, "boss"),
    ("boss_30_die", "Barrett", "그락은… 쓰러지지 않는다….", -3, "boss"),
    ("boss_31_spawn", "Sterling", "서약 따위, 오래전에 버렸다.", -2, "hall"),
    ("boss_31_die", "Sterling", "새벽이… 보이는군….", -2, "hall"),
    ("boss_33_spawn", "Gideon", "하찮은 것들이 바트라의 불을 탐하는가.", -4, "boss"),
    ("boss_33_die", "Gideon", "불은… 꺼지지 않는다….", -4, "boss"),
    ("boss_34_spawn", "Gideon", "모두… 삼켜 주마.", -6, "demon"),
    ("boss_34_die", "Gideon", "아직… 배가… 고프다….", -6, "demon"),
]


def load():
    if os.path.exists(JOBS):
        with open(JOBS, encoding="utf-8") as f:
            return json.load(f)
    return {}


def save(db):
    with open(JOBS, "w", encoding="utf-8") as f:
        json.dump(db, f, ensure_ascii=False, indent=1, sort_keys=True)


def emit():
    db = load()
    out = []
    for i, (key, voice, text, _, _) in enumerate(LINES):
        if db.get(key, {}).get("job"):
            continue
        out.append({"index": i, "params": {"model": "text2speech_v2", "variant": "elevenlabs", "voice_type": "preset",
                                           "voice_id": VOICE[voice], "prompt": text}})
    print(json.dumps(out[:8], ensure_ascii=False))


def register(jobs):
    db = load()
    for j in jobs:
        key = LINES[j["index"]][0]
        db.setdefault(key, {})["job"] = j["job_id"]
        if j.get("result_url"):
            db[key]["url"] = j["result_url"]
    save(db)
    print("registered", len(jobs))


def fetch(raw):
    os.makedirs(raw, exist_ok=True)
    db = load()
    import ssl
    ctx = ssl.create_default_context(cafile="/root/.ccr/ca-bundle.crt") if os.path.exists("/root/.ccr/ca-bundle.crt") else None
    n = 0
    for key, v in db.items():
        url = v.get("url")
        if not url:
            continue
        ext = url.rsplit(".", 1)[-1].split("?")[0] or "mp3"
        path = os.path.join(raw, f"{key}.{ext}")
        if os.path.exists(path):
            continue
        with urllib.request.urlopen(url, context=ctx, timeout=60) as r:
            open(path, "wb").write(r.read())
        n += 1
    print("downloaded", n)


def process(raw, assets):
    out = os.path.join(assets, "voice")
    os.makedirs(out, exist_ok=True)
    files = {os.path.splitext(f)[0]: os.path.join(raw, f) for f in os.listdir(raw)}
    n = 0
    for key, _, _, semis, room in LINES:
        src = files.get(key)
        if not src:
            continue
        chain = []
        # 앞뒤 무음 자르기
        chain.append("silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05")
        chain.append("areverse,silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.08,areverse")
        if semis:
            k = 2 ** (semis / 12.0)
            chain.append(f"asetrate=44100*{k:.5f},aresample=44100,atempo={1.0 / k:.5f}")
        if room == "hall":
            chain.append("aecho=0.8:0.6:45|90:0.25|0.15")
        elif room == "boss":
            chain.append("lowpass=f=6500,aecho=0.8:0.75:70|140|260:0.35|0.25|0.15")
        elif room == "demon":
            chain.append("lowpass=f=5200,acompressor=threshold=-18dB:ratio=6:attack=5:release=80,aecho=0.8:0.8:55|110|220:0.4|0.3|0.2")
        chain.append("loudnorm=I=-16:LRA=7:TP=-1.5")
        chain.append("apad=pad_dur=0.12")
        dst = os.path.join(out, key + ".ogg")
        cmd = ["ffmpeg", "-v", "error", "-y", "-i", src, "-af", ",".join(chain), "-ac", "1", "-ar", "22050",
               "-c:a", "libvorbis", "-q:a", "4", dst]
        subprocess.run(cmd, check=True)
        n += 1
    with open(os.path.join(out, "voice.txt"), "w", encoding="utf-8") as f:
        f.write("# key  text — tools/assetgen/voices.py (Higgsfield text2speech_v2 / ElevenLabs)\n")
        for key, _, text, _, _ in LINES:
            if os.path.exists(os.path.join(out, key + ".ogg")):
                f.write(f"{key}\t{text}\n")
    print("voice lines", n)


def main():
    cmd = sys.argv[1]
    if cmd == "emit":
        emit()
    elif cmd == "register":
        register(json.loads(sys.argv[2]))
    elif cmd == "urls":
        register(json.loads(sys.argv[2]))
    elif cmd == "fetch":
        fetch(sys.argv[2])
    elif cmd == "process":
        process(sys.argv[2], sys.argv[3])


if __name__ == "__main__":
    main()
