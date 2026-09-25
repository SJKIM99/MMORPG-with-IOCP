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
    ('quest', 60, 1), ('questlog', 24, 0), ('questdone', 10, 0), ('discover', 10, 0), ('ending', 12, 0), ('chapter1', 7, 0),
    ('wasd', 10, 0), ('watch', 6, 0), ('runline', 9, 0), ('runcircle', 14, 0), ('behind', 16, 0), ('chat', 6, 0),
    ('aimcombat', 40, 2), ('bigmap', 5, 0), ('keysui', 4, 0), ('keysconflict', 5, 0), ('menu', 4, 0), ('skillwin', 4, 0),
    ('castpreview', 4, 1),
]


def main():
    client, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    only = set(sys.argv[3:])
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, DISPLAY=':99')
    summary = open(os.path.join(out, 'summary.txt'), 'a')
    shots = []
    for name, after, cls in CASES:
        if only and name not in only:
            continue
        who = 'rg_' + name.replace(':', '_')[:12]
        shot = 'rg_%s.png' % name.replace(':', '_')
        cmd = ['./Client2D', '--mute', '--no-msaa', '--size', '1280x720', '--name', who, '--class', str(cls),
               '--script', name, '--screenshot', shot, '--after', str(after)]
        try:
            r = subprocess.run(cmd, cwd=client, env=env, capture_output=True, text=True, timeout=after + 120)
            code = r.returncode
            lines = [l for l in r.stdout.splitlines() if l.startswith('[perf]') or l.startswith('[move-stats]')]
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
