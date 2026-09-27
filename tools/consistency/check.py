#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 일관성 자동 점검(Phase 6) — 이름은 용어집에 있는가 · 쓰지 않는 말 · 색은 토큰인가(보라 · 네온 금지) · 말투
#
#   python3 tools/consistency/check.py [--list]      (저장소 뿌리에서 · 표준 라이브러리만)
#
# 기준 문서: docs/WORLD.md(4장 작명 · 쓰지 않는 말 · 5장 문체 · 6장 공용 용어 · 7장 용어집),
#            docs/DESIGN_SYSTEM.md(색 토큰 — 코드에서는 Client2D/src/ui/Ui.h의 theme).
# 판정: FAIL(고쳐야 한다) · WARN(사람이 본다) · PASS. FAIL이 하나라도 있으면 종료 코드 1.
# 예외는 tools/consistency/allow.txt(한 줄에 "종류 | 값 | 이유") — 이유 없는 예외는 받지 않는다.
# ─────────────────────────────────────────────────────────────────────────────
import colorsys
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
LIST = '--list' in sys.argv

HANGUL = re.compile(r'[가-힣]')
STR_LIT = re.compile(r'u8"((?:[^"\\\n]|\\.)*)"|"((?:[^"\\\n]|\\.)*)"')


def read(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace') as f:
        return f.read()


def strip_comments(src):
    """주석을 지운다(문자열 안의 // 는 남긴다). 줄 수는 그대로."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or (c == 'u' and src.startswith('u8"', i)):
            j = i + (3 if c == 'u' else 1)
            while j < n and src[j] != '"' and src[j] != '\n':
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and src[j] != "'" and src[j] != '\n':
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith('//', i):
            j = src.find('\n', i)
            i = n if j < 0 else j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * src.count('\n', i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def literals(src):
    """(줄 번호, 문자열) — 주석 밖의 문자열 리터럴."""
    code = strip_comments(src)
    for m in STR_LIT.finditer(code):
        s = m.group(1) if m.group(1) is not None else m.group(2)
        yield code.count('\n', 0, m.start()) + 1, s


def table_rows(src, array_re):
    """`inline constexpr T name[] = { {…}, {…} };` 의 맨 위 행마다 행 본문(문자열)."""
    m = re.search(array_re + r'\s*=\s*\{', src)
    if not m:
        return []
    code = strip_comments(src[m.end():])
    rows, depth, start, i = [], 1, None, 0
    while i < len(code) and depth > 0:
        c = code[i]
        if c == '"':
            j = i + 1
            while j < len(code) and code[j] != '"':
                j += 2 if code[j] == '\\' else 1
            i = j + 1
            continue
        if c == '{':
            depth += 1
            if depth == 2:
                start = i
        elif c == '}':
            if depth == 2 and start is not None:
                rows.append(code[start:i + 1])
                start = None
            depth -= 1
        i += 1
    return rows


def first_hangul(row):
    for m in STR_LIT.finditer(row):
        s = m.group(1) if m.group(1) is not None else m.group(2)
        if HANGUL.search(s):
            return s
    return None


# ── 예외 ──
allow = defaultdict(dict)
allow_path = os.path.join(ROOT, 'tools', 'consistency', 'allow.txt')
if os.path.exists(allow_path):
    for line in open(allow_path, encoding='utf-8'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = [p.strip() for p in line.split('|')]
        if len(parts) >= 3 and parts[2]:
            allow[parts[0]][parts[1]] = parts[2]

results = []   # (level, group, text)


def report(level, group, text):
    results.append((level, group, text))


# ─────────────────────────────────────────────────────────────────────────────
# 1. 이름 — 데이터 표의 고유명이 용어집(WORLD.md)에 있는가
# ─────────────────────────────────────────────────────────────────────────────
world = read('docs/WORLD.md')
world_flat = re.sub(r'\s+', '', world)
TABLES = [
    ('지역', 'GameServer/Data/WorldData.h', r'RegionRow\s+kRegions\[[^\]]*\]'),
    ('NPC', 'GameServer/Data/WorldData.h', r'NpcRow\s+kNpcs\[\]'),
    ('구획', 'GameServer/Data/TownMap.h', r'DistrictRow\s+kDistricts\[\]'),
    ('몬스터', 'GameServer/Data/MonsterTable.h', r'MonsterTableRow\s+kRows\[\]'),
    ('아이템', 'GameServer/Item/ItemTableData.h', r'ItemTableRow\s+kRows\[\]'),
    ('기술', 'GameServer/Data/CombatData.h', r'SkillRow\s+kRows\[\]'),
    ('퀘스트', 'GameServer/Data/QuestData.h', r'QuestRow\s+kQuests\[\]'),
    ('장', 'GameServer/Data/QuestData.h', r'Chapter\s+kChapters\[\]'),
]
COMMON_ANIMALS = {'닭'}   # 마을 사람이 부르는 일반명사(용어집 NPC 표에 종류로 있다)
name_total = name_missing = 0
for kind, path, arr in TABLES:
    rows = table_rows(read(path), arr)
    if not rows:
        report('FAIL', '이름', '%s 표를 읽지 못했다: %s %s' % (kind, path, arr))
        continue
    seen = set()
    for row in rows:
        name = first_hangul(row)
        if not name or name in seen:
            continue
        seen.add(name)
        name_total += 1
        if name in COMMON_ANIMALS or name in allow['이름']:
            continue
        if name not in world and re.sub(r'\s+', '', name) not in world_flat:
            name_missing += 1
            report('FAIL', '이름', '%s 「%s」가 용어집(WORLD.md)에 없다 — %s' % (kind, name, path))
report('PASS' if name_missing == 0 else 'INFO', '이름', '데이터 표 고유명 %d개 중 용어집에 없는 것 %d개' % (name_total, name_missing))

# ─────────────────────────────────────────────────────────────────────────────
# 2. 쓰지 않는 말 — 화면에 나가는 한글 문자열(클라 · 공유 데이터 표)
# ─────────────────────────────────────────────────────────────────────────────
BANNED = {
    # WORLD.md 4장 — 클리셰 수식어 · 다른 게임의 고유명
    '에테르': '클리셰', '아르카디아': '클리셰', '루미나': '클리셰', '아르카나': '클리셰', '다크': '클리셰', '블러드': '클리셰',
    '엘리시움': '클리셰', '섀도우': '클리셰', '카오스': '클리셰', '소울': '클리셰', '인페르노': '클리셰', '크리스탈': '클리셰',
    '네크로': '클리셰', '드래곤': '클리셰', '아데나': '다른 게임', '데스나이트': '다른 게임', '바포메트': '다른 게임',
    '집행검': '다른 게임', '오리하루콘': '다른 게임', '라우풀': '다른 게임', '카오틱': '다른 게임', '텔레포터': '다른 게임',
    # WORLD.md 6장 — 영어 음차 대신 쓰는 말이 정해진 것
    '길드': '→ 결사', '옥션': '→ 경매장', '우편함': '→ 정산함', '아이덴티티': '→ 고유 기술', '트라이포드': '→ 갈래',
    '스태거': '→ 무력화', '슈퍼아머': '→ 경직 면역', '엠블럼': '→ 문장', '크레스트': '→ 문장', '스킬': '→ 기술',
    '배틀 아이템': '→ 전투 칸', '길드장': '→ 결사장', '부길드장': '→ 부관', '즉시 구매': '→ 사기', '입찰': '(경매장은 사기뿐)',
    '수령': '→ 받기', '인벤토리': '→ 소지품 · 가방', '쿨타임': '→ 재사용 대기', '쿨다운': '→ 재사용 대기',
    # WORLD.md 3.5 — 영어 음차 대신 번역층 말(물건 · 짐승 이름)
    '포션': '→ 물약', '스크롤': '→ 두루마리', '텔레포트': '→ 옛길 이동', '젤리': '→ -덩이', '골렘': '→ 석인',
    '드레이크': '→ 비룡', '슬레이어': '→ 새벽지기', '로브': '→ 법복', '건틀릿': '→ 장갑', '건틀렛': '→ 장갑',
}
# 짧은 말(오브 · 루나 · 솔라 · 데스 · 헬)은 다른 낱말 안에 흔히 들어 있어(헬멧 · 데스크) 낱말 머리에 올 때만 본다
BANNED_HEAD = {'오브': '클리셰', '루나': '클리셰', '솔라': '클리셰', '데스': '클리셰', '헬': '클리셰', '룬': '→ 각인'}
UI_SOURCES = []
for base in ('Client2D/src',):
    for dp, dn, fn in os.walk(os.path.join(ROOT, base)):
        for f in fn:
            if f.endswith(('.cpp', '.h')):
                UI_SOURCES.append(os.path.relpath(os.path.join(dp, f), ROOT))
for f in ('WorldData.h', 'NpcTalk.h', 'QuestData.h', 'CombatData.h', 'SkillData.h', 'SkillGrowth.h', 'MonsterTable.h', 'GuildAuction.h', 'ItemRules.h'):
    UI_SOURCES.append('GameServer/Data/' + f)
UI_SOURCES.append('GameServer/Item/ItemTableData.h')
UI_SOURCES.sort()
banned_hits = 0
screen_strings = []   # (파일, 줄, 문자열) — 3 · 4장에서도 쓴다
for rel in UI_SOURCES:
    src = read(rel)
    for line, s in literals(src):
        if not HANGUL.search(s):
            continue
        screen_strings.append((rel, line, s))
        for w, why in BANNED.items():
            if w in s and w not in allow['말']:
                banned_hits += 1
                report('FAIL', '쓰지 않는 말', '%s:%d 「%s」 %s — "%s"' % (rel, line, w, why, s[:60]))
        for w, why in BANNED_HEAD.items():
            if re.search(r'(^|[\s·(「"\'])' + w, s) and w not in allow['말']:
                banned_hits += 1
                report('FAIL', '쓰지 않는 말', '%s:%d 「%s」 %s — "%s"' % (rel, line, w, why, s[:60]))
report('PASS' if banned_hits == 0 else 'INFO', '쓰지 않는 말', '화면 문자열 %d개 · 걸린 곳 %d' % (len(screen_strings), banned_hits))

# ─────────────────────────────────────────────────────────────────────────────
# 3. 색 — 보라 · 네온 금지(모든 색 값) · UI 코드의 토큰 밖 색(WARN)
# ─────────────────────────────────────────────────────────────────────────────
ui_h = read('Client2D/src/ui/Ui.h')
tokens = {}
for m in re.finditer(r'inline constexpr Color (k\w+)\{\s*(\d+),\s*(\d+),\s*(\d+)', ui_h):
    tokens[(int(m.group(2)), int(m.group(3)), int(m.group(4)))] = m.group(1)

COLOR_PATTERNS = [
    re.compile(r'Rgb\(\s*0x([0-9A-Fa-f]{6})'),
    re.compile(r'Color\s*\{\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})'),
    re.compile(r'(?<![\w.])0x([0-9A-Fa-f]{6})(?![0-9A-Fa-f])'),
]


def colors_in(code):
    seen = set()   # 같은 자리를 두 패턴이 잡으면(Rgb(0x…) 안의 0x…) 한 번만
    for pat_i, pat in enumerate(COLOR_PATTERNS):
        for m in pat.finditer(code):
            if pat_i == 1:
                rgb = tuple(int(m.group(k)) for k in (1, 2, 3))
                if max(rgb) > 255:
                    continue
            else:
                v = int(m.group(1), 16)
                rgb = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
            line = code.count('\n', 0, m.start()) + 1
            if (line, rgb) in seen:
                continue
            seen.add((line, rgb))
            yield line, rgb, pat_i


def hsv(rgb):
    h, s, v = colorsys.rgb_to_hsv(*(c / 255.0 for c in rgb))
    return h * 360.0, s, v


def is_purple(rgb):
    # 보라: 파랑과 자홍 사이(색상 255°~320°), 채도 · 밝기가 어느 정도 있을 때 — 먹빛 · 잿빛(채도 낮음)은 괜찮다
    h, s, v = hsv(rgb)
    return 255.0 <= h <= 320.0 and s >= 0.30 and v >= 0.30


def is_neon(rgb):
    # 네온: 거의 순색에 가까운 밝은 녹 · 청록 · 자홍(불 · 금 · 피 같은 따뜻한 색은 전투 연출의 몫이라 뺀다)
    h, s, v = hsv(rgb)
    return s >= 0.85 and v >= 0.92 and (75.0 <= h <= 200.0 or 280.0 <= h <= 345.0)


purple = neon = 0
color_files = [p for p in UI_SOURCES]
for p in ('GameServer/Data/WorldData.h', 'GameServer/Data/MonsterTable.h', 'GameServer/Data/CombatData.h'):
    if p not in color_files:
        color_files.append(p)
for rel in color_files:
    code = strip_comments(read(rel))
    for line, rgb, kind in colors_in(code):
        key = '#%02X%02X%02X' % rgb
        if kind == 2 and not re.search(r'color|Color|Rgb|col|tint|0x[0-9A-Fa-f]{6}\s*[,}]', code.splitlines()[line - 1] if line - 1 < len(code.splitlines()) else ''):
            continue   # 색이 아닐 수 있는 16진수(비트 마스크 등)
        if is_purple(rgb) and key not in allow['색']:
            purple += 1
            report('FAIL', '색', '%s:%d %s 보라(색상 %.0f°) — DESIGN_SYSTEM: 보라 · 네온 금지' % (rel, line, key, hsv(rgb)[0]))
        elif is_neon(rgb) and key not in allow['색']:
            neon += 1
            report('FAIL', '색', '%s:%d %s 네온(채도 %.2f · 밝기 %.2f) — DESIGN_SYSTEM: 보라 · 네온 금지' % (rel, line, key, hsv(rgb)[1], hsv(rgb)[2]))
report('PASS' if purple + neon == 0 else 'INFO', '색', '보라 %d · 네온 %d' % (purple, neon))

# UI 코드(창 · HUD · 채팅 · 결사 · 경매)의 토큰 밖 색 — 가장 가까운 토큰과 함께
UI_FILES = [p for p in UI_SOURCES if re.search(r'Client2D/src/(ui/|game/Game(Ui|Hud|Windows|Guild|Auction|Chat|Items|Quest|Skill)\.cpp|game/Chat)', p)]


def nearest_token(rgb):
    best, bd = None, 1e9
    for t, name in tokens.items():
        d = sum((a - b) ** 2 for a, b in zip(rgb, t)) ** 0.5
        if d < bd:
            best, bd = name, d
    return best, bd


offtoken = defaultdict(list)
ui_colors = 0
for rel in UI_FILES:
    code = strip_comments(read(rel))
    for line, rgb, kind in colors_in(code):
        if kind == 2:
            continue
        ui_colors += 1
        if rgb in tokens:
            continue
        name, d = nearest_token(rgb)
        offtoken[rel].append((line, rgb, name, d))
NEAR = 12.5   # RGB 거리 — 이만큼 가까우면 토큰을 흉내 낸 값(토큰으로 바꿔야 한다)
near_items = [(rel, it) for rel, items in offtoken.items() for it in items if it[3] <= NEAR]
art_items = [(rel, it) for rel, items in offtoken.items() for it in items if it[3] > NEAR]
for rel, (line, rgb, name, d) in near_items:
    report('FAIL', '색', '%s:%d #%02X%02X%02X — 토큰 %s와 거의 같다(거리 %.0f) → ui::theme::%s' % ((rel, line) + rgb + (name, d, name)))
report('PASS' if not near_items else 'INFO', '색', 'UI 코드에 적힌 색 값(리터럴) %d개 — 토큰을 흉내 낸 값 %d개' % (ui_colors, len(near_items)))
# 토큰과 뚜렷이 다른 값은 창 안의 그림 · 연출 색(아이콘 · 시연 화면 · 장 카드 · 결사 문장 등)이다 — DESIGN_SYSTEM 1장 "그림 색".
# 보라 · 네온 검사는 위에서 이미 받았다. 여기서는 수와 파일만 알린다(--list로 목록).
report('INFO', '색', '그림 · 연출 색(토큰과 뚜렷이 다른 값) %d개 · 파일 %d — 보라 · 네온 검사만 받는다' % (len(art_items), len({r for r, _ in art_items})))
if LIST:
    by_file = defaultdict(list)
    for rel, it in art_items:
        by_file[rel].append(it)
    for rel, items in sorted(by_file.items(), key=lambda kv: -len(kv[1])):
        report('INFO', '색', '%s: 그림 · 연출 색 %d개' % (rel, len(items)))
        for line, rgb, name, d in items[:12]:
            report('INFO', '색', '    %d #%02X%02X%02X (가까운 토큰 %s · 거리 %.0f)' % ((line,) + rgb + (name, d)))

# ─────────────────────────────────────────────────────────────────────────────
# 4. 말투 — 시스템 문구(느낌표 · 물결 없음) · NPC 말투(WORLD.md 5장)
# ─────────────────────────────────────────────────────────────────────────────
SYSTEM_CALL = re.compile(r'(AddCenter|AddBanner|AddChat|SystemLine|AddSystem)\s*\(\s*(?:Fmt\s*\(\s*)?(?:u8)?"((?:[^"\\\n]|\\.)*)"')
sys_total = sys_bad = 0
for rel in UI_SOURCES:
    if not rel.startswith('Client2D/'):
        continue
    code = strip_comments(read(rel))
    for m in SYSTEM_CALL.finditer(code):
        s = m.group(2)
        if not HANGUL.search(s):
            continue
        sys_total += 1
        line = code.count('\n', 0, m.start()) + 1
        if ('!' in s or '~' in s) and s not in allow['말투']:
            sys_bad += 1
            report('FAIL', '말투', '%s:%d 시스템 문구에 느낌표 · 물결 — "%s" (5장: 명사형 · 짧은 평서문, 감탄사 없음)' % (rel, line, s[:70]))
report('PASS' if sys_bad == 0 else 'INFO', '말투', '시스템 문구 %d개 · 걸린 것 %d' % (sys_total, sys_bad))

# NPC 말투: 종류 → 기대 말투(문장 끝)
REGISTER = {
    '하오체': re.compile(r'(오|소|시오|구려|이오|하오|겠소|했소|있소|없소|마시오|주시오)[.?!…」"\']*$'),
    '하게체': re.compile(r'(게|네|세|지|가|나|라네|다네|걸세|하게|이네|구먼|는가|겠나|보게|주게|일세)[.?!…」"\']*$'),
    '해요체': re.compile(r'(요|죠|까요|세요|에요|예요|어요|해요|네요|군요|래요|지요|나요|니다|니까)[.?!…」"\']*$'),   # 격식 있는 해요체(길잡이)는 -ㅂ니다를 섞는다
}
NPC_REGISTER = {   # WORLD.md 5장
    '경비대장': '하오체', '경비병': '하오체', '순찰병': '하오체', '파수꾼': '하오체', '교관': '하오체',
    '촌장': '하게체', '대장장이': '하게체', '방어구장이': '하게체',
    '사제': '해요체', '여관 주인': '해요체', '잡화상': '해요체', '길잡이': '해요체', '선술집 주인': '해요체', '약초지기': '해요체', '빨래꾼': '해요체',
    '경매인': '해요체',
}
npc_rows = table_rows(read('GameServer/Data/WorldData.h'), r'NpcRow\s+kNpcs\[\]')
tone_total = tone_warn = 0
for row in npc_rows:
    lits = [m.group(1) if m.group(1) is not None else m.group(2) for m in STR_LIT.finditer(row)]
    lits = [s for s in lits if HANGUL.search(s)]
    if len(lits) < 2:
        continue
    name, greet = lits[0], lits[1]
    reg = next((r for title, r in NPC_REGISTER.items() if name.startswith(title + ' ')), None)
    if not reg:
        continue
    for sent in re.split(r'(?<=[.?!…])\s+', greet.strip()):
        if not HANGUL.search(sent):
            continue
        tone_total += 1
        if not REGISTER[reg].search(sent.strip()) and sent not in allow['말투']:
            tone_warn += 1
            report('WARN', '말투', '%s(%s) 인사 "%s" — 문장 끝이 %s가 아닌 것 같다' % (name, reg, sent[:50], reg))
report('PASS' if tone_warn == 0 else 'WARN', '말투', 'NPC 인사 문장 %d개 · 말투 확인할 것 %d' % (tone_total, tone_warn))

# ─────────────────────────────────────────────────────────────────────────────
order = {'FAIL': 0, 'WARN': 1, 'INFO': 2, 'PASS': 3}
for level, group, text in sorted(results, key=lambda r: (order[r[0]], r[1])) if not LIST else results:
    print('%-4s [%s] %s' % (level, group, text))
fails = sum(1 for r in results if r[0] == 'FAIL')
warns = sum(1 for r in results if r[0] == 'WARN')
print('RESULT fail=%d warn=%d' % (fails, warns))
sys.exit(1 if fails else 0)
