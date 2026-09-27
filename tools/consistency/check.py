#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/consistency/check.py — 하르벤(newclient) 일관성 자동 점검 (Phase 6-A)

WORLD.md / DESIGN_SYSTEM.md 에 정한 브랜드 규칙을 코드·데이터에 대해 기계적으로 검사한다.
FAIL 이 하나라도 있으면 종료 코드 1. `allow.txt` 에 올린 줄은 예외로 건너뛴다(이유를 반드시 주석으로).

검사 항목
  1. 쓰지 않는 말   — WORLD.md 4장 클리셰/타게임 고유명 + 6장 영어 음차
  2. 보라 · 네온색  — 코드에 박힌 Color{r,g,b[,a]} 리터럴의 HSV
  3. 토큰 흉내      — 그 리터럴이 ui::theme 토큰과 채널마다 12.5 이내로 가까운데 토큰을 안 쓴 경우
  4. 말투(느낌표)   — 시스템/알림성 문구로 보이는 한국어 문자열에 '!' 가 들어간 경우
  5. 용어집(느슨함) — 데이터 테이블의 한국어 고유명이 WORLD.md 용어집 표에 있는지(최선 노력)

사용법: python3 tools/consistency/check.py [--list] [--json out.json]
  --list  FAIL 각 건을 전부 출력(기본은 항목별 개수만).
"""
import re
import sys
import os
import json
import argparse

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# ---------------------------------------------------------------- 대상 파일
CODE_EXT = (".cpp", ".h", ".hpp", ".inl")
SKIP_DIR_PARTS = {
    "packages", ".git", ".vs", "third_party", "Binary", "build",
    "Claude outputs", "assets" + os.sep + "fonts",
}


def iter_files(base, exts):
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = [d for d in dirnames if d not in
                       {"packages", ".git", ".vs", "third_party", "Binary", "build"}]
        if "Claude outputs" in dirpath:
            continue
        for fn in filenames:
            if fn.endswith(exts):
                yield os.path.join(dirpath, fn)


def load_allow(path):
    allow = set()
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if line:
                allow.add(line)
    return allow


# ---------------------------------------------------------------- 1. 쓰지 않는 말
CLICHE_WORDS = [
    "에테르", "아르카디아", "루미나", "아르카나", "다크", "블러드", "엘리시움",
    "섀도우", "카오스", "소울", "인페르노", "크리스탈", "루나", "솔라",
    "네크로", "드래곤", "데스", "헬",
]
OTHER_GAME_NAMES = [
    "아데나", "데스나이트", "바포메트", "집행검", "오리하루콘",
    "드래곤 슬레이어", "라우풀", "카오틱", "텔레포터",
]
LOANWORDS = [
    "젤리", "골렘", "드레이크", "슬레이어", "포션", "스크롤", "텔레포트", "룬",
    "길드", "옥션", "우편함", "로브", "건틀릿",
]
BANNED = [(w, "클리셰") for w in CLICHE_WORDS] + \
         [(w, "타게임 고유명") for w in OTHER_GAME_NAMES] + \
         [(w, "영어 음차") for w in LOANWORDS]

# 화면에 나오는 문자열 리터럴만 검사한다("..." 안). 설계 메모·구현 주석은
# WORLD.md 자신도 "스펙 용어를 그대로 쓴다"고 허용한 영역이라 대상에서 뺀다
# — 그래서 // 주석 안의 '텔레포트' 같은 표현은 FAIL 이 아니다(별도로 세어 참고만 한다).
STRING_LIT_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
# 짧은 낱말(1~2음절)은 다른 한국어 낱말 속에 우연히 들어있을 수 있어
# (예: '다룬다' 속의 '룬', '이룬' 속의 '룬') 한글 음절 경계를 두고 찾는다.
SHORT_WORDS = {"룬", "포션", "로브"}


def _find_in(text, word):
    if word in SHORT_WORDS:
        return re.search(rf"(?<![가-힣]){re.escape(word)}(?![가-힣])", text) is not None
    return word in text


def check_banned_words(files, allow):
    fails = []
    comment_only = 0
    for path in files:
        try:
            text = open(path, encoding="utf-8", errors="ignore").read()
        except OSError:
            continue
        lines = text.splitlines()
        for i, line in enumerate(lines, 1):
            key = f"{os.path.relpath(path, ROOT)}:{i}"
            if key in allow:
                continue
            strings = STRING_LIT_RE.findall(line)
            in_string = False
            for word, kind in BANNED:
                for s in strings:
                    if _find_in(s, word):
                        fails.append((path, i, kind, word, line.strip()[:100]))
                        in_string = True
                        break
            if not in_string:
                for word, kind in BANNED:
                    if _find_in(line, word):
                        comment_only += 1
                        break
    return fails, comment_only


# ---------------------------------------------------------------- 2·3. 색
COLOR_LIT_RE = re.compile(
    r"\bColor\s*[\{\(]\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*(?:,\s*(\d{1,3}))?\s*[\}\)]"
)
TOKEN_DECL_RE = re.compile(
    r"inline\s+constexpr\s+Color\s+(k\w+)\s*\{\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})"
)
# DESIGN_SYSTEM 0장4/8장1 — 토큰 강제 규칙은 UI 화면(창·버튼·글자·HUD)에 적용한다.
# 9장(게임 그림 — 캐릭터·몬스터·배경·소품·이펙트를 손으로 그리는 절차적 아트)은
# 넓은 팔레트를 쓰는 것이 설계 의도라 별도 항목(색 개수만 보고, FAIL 아님)으로 뺀다.
UI_TOKEN_SCOPE = ("Client2D/src/ui",)
PROCEDURAL_ART_SCOPE = ("Client2D/src/render", "Client2D/src/game")


def rgb_to_hsv(r, g, b):
    r, g, b = r / 255.0, g / 255.0, b / 255.0
    mx, mn = max(r, g, b), min(r, g, b)
    v = mx
    d = mx - mn
    s = 0 if mx == 0 else d / mx
    if d == 0:
        h = 0
    elif mx == r:
        h = (60 * ((g - b) / d) + 360) % 360
    elif mx == g:
        h = (60 * ((b - r) / d) + 120) % 360
    else:
        h = (60 * ((r - g) / d) + 240) % 360
    return h, s, v


def load_tokens(ui_h_path):
    tokens = {}
    text = open(ui_h_path, encoding="utf-8", errors="ignore").read()
    for m in TOKEN_DECL_RE.finditer(text):
        name, r, g, b = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
        tokens[name] = (r, g, b)
    return tokens


def in_scope(path, scopes):
    rel = os.path.relpath(path, ROOT).replace("\\", "/")
    return any(rel.startswith(s) for s in scopes)


def check_colors(files, tokens, allow):
    purple_neon = []
    mimic = []
    raw_in_ui = []
    for path in files:
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        if rel == "Client2D/src/ui/Ui.h":
            continue  # 토큰 선언 자체는 제외
        try:
            lines = open(path, encoding="utf-8", errors="ignore").read().splitlines()
        except OSError:
            continue
        for i, line in enumerate(lines, 1):
            key = f"{rel}:{i}"
            if key in allow:
                continue
            for m in COLOR_LIT_RE.finditer(line):
                r, g, b = int(m.group(1)), int(m.group(2)), int(m.group(3))
                h, s, v = rgb_to_hsv(r, g, b)
                is_purple = 265 <= h <= 335 and s >= 0.20 and v >= 0.20
                # 네온 = 채도 · 명도가 아주 높은 초록~하늘색 계열(합성 형광색).
                # 불씨 · 피 · 새벽빛 같은 따뜻한 색(주황~빨강, h<60)은 게임 속에서 늘 밝고
                # 진하게 쓰이므로(9장 STYLE_FX) 네온으로 잘못 잡지 않는다.
                is_neon = s >= 0.85 and v >= 0.90 and 70 <= h <= 200
                if is_purple:
                    purple_neon.append((path, i, "보라", (r, g, b), line.strip()[:100]))
                elif is_neon:
                    purple_neon.append((path, i, "네온", (r, g, b), line.strip()[:100]))
                nearest = None
                for name, (tr, tg, tb) in tokens.items():
                    dist = max(abs(r - tr), abs(g - tg), abs(b - tb))
                    if dist <= 12.5 and (nearest is None or dist < nearest[1]):
                        nearest = (name, dist)
                if nearest:
                    mimic.append((path, i, nearest[0], (r, g, b), line.strip()[:100]))
                elif in_scope(path, UI_TOKEN_SCOPE):
                    raw_in_ui.append((path, i, (r, g, b), line.strip()[:100]))
    return purple_neon, mimic, raw_in_ui


# ---------------------------------------------------------------- 4. 말투(느낌표)
KOREAN_STR_RE = re.compile(r'"([^"\\]|\\.)*[가-힣]([^"\\]|\\.)*"')
SYSTEM_FILE_HINTS = ("Toast", "Notify", "Hud", "System", "Chat", "GameDebug")
DANGER_ALLOW_HINT = ("벗어나", "위험", "피하")  # 위험 경고는 5장 규칙상 느낌표 허용


def check_tone(files, allow):
    fails = []
    for path in files:
        base = os.path.basename(path)
        if not any(h in base for h in SYSTEM_FILE_HINTS):
            continue
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        try:
            lines = open(path, encoding="utf-8", errors="ignore").read().splitlines()
        except OSError:
            continue
        for i, line in enumerate(lines, 1):
            key = f"{rel}:{i}"
            if key in allow:
                continue
            for m in KOREAN_STR_RE.finditer(line):
                s = m.group(0)
                if "!" in s and not any(h in s for h in DANGER_ALLOW_HINT):
                    fails.append((path, i, s[:80]))
    return fails


# ---------------------------------------------------------------- 5. 용어집(느슨함, best-effort)
GLOSSARY_TABLE_RE = re.compile(r"^\|\s*\d+\s*\|\s*([^|]+?)\s*\|", re.M)


def load_glossary(world_md_path):
    text = open(world_md_path, encoding="utf-8", errors="ignore").read()
    # 7장부터 끝까지만 본다(그 앞은 규칙 설명 표라 id 형식이 다르다).
    idx = text.find("## 7. 용어집")
    section = text[idx:] if idx >= 0 else text
    names = set()
    for m in GLOSSARY_TABLE_RE.finditer(section):
        name = m.group(1).strip()
        name = re.sub(r"\*\*", "", name)
        if name and "예" not in name and "뜻" not in name:
            names.add(name)
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--json")
    args = ap.parse_args()

    allow_path = os.path.join(ROOT, "tools", "consistency", "allow.txt")
    allow = load_allow(allow_path)

    ui_h = os.path.join(ROOT, "Client2D", "src", "ui", "Ui.h")
    world_md = os.path.join(ROOT, "docs", "WORLD.md")

    code_files = list(iter_files(os.path.join(ROOT, "Client2D", "src"), CODE_EXT)) + \
                 list(iter_files(os.path.join(ROOT, "GameServer"), CODE_EXT))

    banned, banned_comment_only = check_banned_words(code_files, allow)
    tokens = load_tokens(ui_h) if os.path.exists(ui_h) else {}
    purple_neon, mimic, raw_in_ui = check_colors(code_files, tokens, allow)
    tone = check_tone(code_files, allow)
    glossary = load_glossary(world_md) if os.path.exists(world_md) else set()

    total_fail = len(banned) + len(purple_neon) + len(mimic) + len(tone)

    print("=== 하르벤 일관성 점검 (tools/consistency/check.py) ===")
    print(f"검사 파일 수      : {len(code_files)}")
    print(f"토큰 수(Ui.h)     : {len(tokens)}")
    print(f"용어집 고유명 수  : {len(glossary)}  (best-effort — 7장 표 파싱)")
    print("-" * 56)
    print(f"쓰지 않는 말      : {len(banned)}  (문자열 리터럴만. 주석 안 언급 {banned_comment_only}건은 별도 — FAIL 아님)")
    print(f"보라 · 네온       : {len(purple_neon)}")
    print(f"토큰 흉내         : {len(mimic)}")
    print(f"토큰 밖 raw(UI)   : {len(raw_in_ui)}  (참고 — FAIL 아님, ui/ 안의 미등록 색)")
    print(f"말투(느낌표)      : {len(tone)}")
    print("-" * 56)
    print(f"FAIL 합계         : {total_fail}")

    if args.list:
        def dump(title, rows, fmt):
            if not rows:
                return
            print(f"\n-- {title} --")
            for row in rows:
                print(fmt(row))

        dump("쓰지 않는 말", banned,
             lambda r: f"{os.path.relpath(r[0], ROOT)}:{r[1]}  [{r[2]}] '{r[3]}'  {r[4]}")
        dump("보라 · 네온", purple_neon,
             lambda r: f"{os.path.relpath(r[0], ROOT)}:{r[1]}  [{r[2]}] rgb{r[3]}  {r[4]}")
        dump("토큰 흉내", mimic,
             lambda r: f"{os.path.relpath(r[0], ROOT)}:{r[1]}  ~{r[2]} 와 근접 rgb{r[3]}  {r[4]}")
        dump("말투(느낌표)", tone,
             lambda r: f"{os.path.relpath(r[0], ROOT)}:{r[1]}  {r[2]}")

    if args.json:
        payload = {
            "banned": [[os.path.relpath(p, ROOT), i, k, w, s] for p, i, k, w, s in banned],
            "purple_neon": [[os.path.relpath(p, ROOT), i, k, list(c), s] for p, i, k, c, s in purple_neon],
            "mimic": [[os.path.relpath(p, ROOT), i, tk, list(c), s] for p, i, tk, c, s in mimic],
            "tone": [[os.path.relpath(p, ROOT), i, s] for p, i, s in tone],
            "raw_in_ui_count": len(raw_in_ui),
            "glossary_count": len(glossary),
            "fail_total": total_fail,
        }
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)

    sys.exit(1 if total_fail > 0 else 0)


if __name__ == "__main__":
    main()
