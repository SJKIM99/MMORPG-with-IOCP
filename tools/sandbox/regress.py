#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 회귀 검사 — 모든 --script 시나리오를 차례로 돌려 스크린샷 한 장씩 + 종료 상태 + 성능 줄을 모은다.
#
#   regress.py <클라 폴더> <출력 폴더> [시나리오 …]      (서버는 먼저 띄워 둔다: run.sh server)
#
# 결과: <출력>/<시나리오>.png, summary.txt(종료 코드 · perf · move-stats), sheet_N.jpg(12장씩 모은 판)
# 캐릭터 이름은 시나리오마다 따로(rg_<시나리오>) — 서로의 상태(퀘스트 · 가방)를 건드리지 않는다.
# ─────────────────────────────────────────────────────────────────────────────
import os
import subprocess
import sys

# (시나리오, 스크린샷 시각(초), 직업)
CASES = [
    ('hunt', 40, 0), ('town', 16, 0), ('inventory', 6, 0), ('map', 6, 0), ('help', 6, 0), ('enchant', 12, 0),
    ('teleport', 26, 1), ('boss', 46, 0), ('skill', 40, 1), ('combat', 40, 2), ('loot', 46, 0), ('gear', 10, 0),
    ('gearsort', 10, 0), ('lootshow', 9, 0), ('smith', 14, 0), ('charwin', 6, 0), ('stand', 6, 1), ('plaza', 9, 2),
    ('talk', 16, 0), ('priest', 16, 1), ('pet', 16, 2), ('soup', 16, 0), ('npc', 14, 0), ('teleportwin', 16, 0),
    ('quest', 60, 1), ('questlog', 24, 0), ('questdone', 10, 0), ('discover', 10, 0), ('ending', 12, 0), ('chapter1', 1.3, 0),   # 장 카드는 실제 시계로 5.2초 — 느린 샌드박스(프레임 dt 상한 0.05)에서는 달린 시간 약 1.1~1.5에만 보인다
    ('wasd', 10, 0), ('watch', 6, 0), ('runline', 9, 0), ('runcircle', 14, 0), ('behind', 16, 0), ('chat', 6, 0),
    ('aimcombat', 40, 2), ('bigmap', 5, 0), ('keysui', 4, 0), ('keysconflict', 5, 0), ('menu', 4, 0), ('skillwin', 4, 0),
    ('castpreview', 4, 1), ('skills', 45, 0),
    # Phase 2-D · 2-E: 기술 창(레벨 · 갈래 · 시연) · 훈련장 허수아비(서약기사 · 밤사냥꾼)
    ('skillgrow', 24, 1), ('training', 45, 0), ('skills:2', 45, 2),
    # 기술 성장 서버 규칙(레벨 3 캐릭터 — 점수 4): 15단계 기대값 비교, 로그의 [growcheck] done pass=15 fail=0
    ('growcheck', 55, 0, 'lv3_rg_gc'), ('awaken3', 16, 0, 'lv50_rg_aw'),
    # Phase 3-A · 3-C: 창 관리자(끌기 · 크기 · 자석 · 겹침 순서 · Esc · 저장) · UI 편집(옮기기 · 배율) · HUD 겹침 검사
    ('wm', 16, 0), ('uiedit', 14, 0), ('hudres', 24, 0),
    # Phase 3 자기 검토: 채팅창 설정이 재접속 뒤에도 그대로(같은 이름 두 번) · 귓속말 이름 완성 · 조용하면 흐려지기
    ('chatcfg', 8, 0, 'rg_chatcfg'), ('chatcfg2', 30, 0, 'rg_chatcfg'),
    # 기술 칸 손맛: 재사용 대기 중 누름(눌림 · 붉은 반짝 · 안내 1초에 한 번) · 자원 부족(레벨 30 — 모든 칸이 열린 캐릭터)
    ('hotfeel', 45, 0, 'lv30_rg_hotfeel'),   # Phase 4 뒤: 첫 접속 자리 → 훈련장 걷기가 길어져 20 → 45초
    # Phase 4-A 높낮이: 경계 칸 전수 검사(서버와 같은 TownRules::Check) · 보이지 않는 벽 · 계단 우회 탐침 — [terraincheck] done PASS
    ('terraincheck', 90, 0),
    # Phase 4 자기 검토: 남문 도착 연출([arrive] done) · 비둘기 떼 날려 보내고 다시 내려앉기([birds] done PASS)
    ('arrive', 45, 0), ('birds', 60, 0),
    # Phase 6 자기 검토: 눈밭 보스전 — 대상 보스 머리 위 글자 한 층 · 밝은 바닥 섞기(흰 장면)
    ('boss3', 30, 0, 'lv50_rg_boss3'),
]


def main():
    client, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    only = set(sys.argv[3:])
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, DISPLAY=':99')
    summary = open(os.path.join(out, 'summary.txt'), 'a')
    shots = []
    for case in CASES:
        name, after, cls = case[0], case[1], case[2]
        if only and name not in only:
            continue
        who = case[3] if len(case) > 3 else 'rg_' + name.replace(':', '_')[:12]
        shot = 'rg_%s.png' % name.replace(':', '_')
        cmd = ['./Client2D', '--mute', '--no-msaa', '--size', '1280x720', '--name', who, '--class', str(cls),
               '--script', name, '--screenshot', shot, '--after', str(after)]
        try:
            r = subprocess.run(cmd, cwd=client, env=env, capture_output=True, text=True, timeout=after * 5 + 120)   # 소프트웨어 렌더링(5fps 안팎)에서도 넉넉히
            code = r.returncode
            lines = [l for l in r.stdout.splitlines() if l.startswith('[perf]') or l.startswith('[move-stats]') or l.startswith('[growcheck] done') or l.startswith('[bench]') or l.startswith('[wmcheck] done') or l.startswith('[hudcheck] screen') or l.startswith('[terraincheck] done') or l.startswith('[birds] done') or l.startswith('[arrive] done')]
        except subprocess.TimeoutExpired:
            code, lines = 'TIMEOUT', []
        src = os.path.join(client, shot)
        dst = os.path.join(out, name.replace(':', '_') + '.png')
        ok = os.path.exists(src)
        if ok:
            os.replace(src, dst)
            shots.append((name, dst))
        summary.write('%-14s exit=%s shot=%s\n' % (name, code, 'ok' if ok else 'MISSING'))
        for l in lines:
            summary.write('    ' + l[:160] + '\n')
        summary.flush()
        print(name, code, 'ok' if ok else 'MISSING', flush=True)
    summary.close()
    # 12장씩 모은 판
    try:
        from PIL import Image, ImageDraw
        for i in range(0, len(shots), 12):
            chunk = shots[i:i + 12]
            sheet = Image.new('RGB', (4 * 480, 3 * 290), (17, 21, 25))
            d = ImageDraw.Draw(sheet)
            for j, (name, path) in enumerate(chunk):
                im = Image.open(path).convert('RGB').resize((480, 270))
                x, y = (j % 4) * 480, (j // 4) * 290
                sheet.paste(im, (x, y + 20))
                d.text((x + 6, y + 4), name, fill=(237, 231, 219))
            sheet.save(os.path.join(out, 'sheet_%d.jpg' % (i // 12 + 1)), quality=82)
    except Exception as e:  # noqa
        print('sheet 실패', e)


if __name__ == '__main__':
    main()
