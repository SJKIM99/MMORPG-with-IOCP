#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 실제 키보드 · 마우스로 조작 검증(Phase 1-A) — Xvfb 위의 Client2D에 xdotool로 진짜 입력을 넣는다.
#
#   input_test.py <클라 폴더> <출력 폴더> [이름]
#
# 차례: 로그인 → Esc(메뉴) → 조작 설정 → 기술 1 = T로 → 기술 2 칸에서 E(겹침 경고) → 맞바꾸기
#      → 닫기 · 종료 → 다시 접속(저장 복원 확인) → Enter 채팅에 "qwer asdf" 입력(기술이 나가지 않아야) → 보내기
#      → I · K · Tab 창 열기 → 스크린샷. 결과 파일: controls_<이름>.txt 내용, shot_*.png
# 한 프레임이 느려도(소프트웨어 렌더링) 눌림이 잡히게 키는 0.35초씩 누른다.
# ─────────────────────────────────────────────────────────────────────────────
import os
import subprocess
import sys
import time

W, H = 1280, 720
ENV = dict(os.environ, DISPLAY=':99')


def xdo(*args):
    return subprocess.run(['xdotool', *args], env=ENV, capture_output=True, text=True).stdout.strip()


def win():
    for _ in range(80):
        ids = xdo('search', '--name', '하르벤').split()
        if ids:
            return ids[-1]
        time.sleep(0.25)
    raise SystemExit('창을 못 찾음')


def key(k, hold=0.35):
    xdo('keydown', k)
    time.sleep(hold)
    xdo('keyup', k)
    time.sleep(0.35)


def click(wid, x, y, button=1):
    xdo('mousemove', '--window', wid, str(x), str(y))
    time.sleep(0.4)
    xdo('mousedown', str(button))
    time.sleep(0.35)
    xdo('mouseup', str(button))
    time.sleep(0.5)


def shot(out, name):
    path = os.path.join(out, 'shot_%s.png' % name)
    subprocess.run(['import', '-window', 'root', '-crop', '%dx%d+0+0' % (W, H), path], env=ENV)
    print('shot', path)


def start(client_dir, name, log):
    p = subprocess.Popen(['./Client2D', '--mute', '--no-msaa', '--name', name, '--class', '1', '--size', '%dx%d' % (W, H)],
                         cwd=client_dir, env=ENV, stdout=log, stderr=subprocess.STDOUT)
    wid = win()
    xdo('windowmove', wid, '0', '0')
    xdo('windowfocus', wid)
    time.sleep(9)   # 접속 · 월드 들어오기
    return p, wid


def key_chip(col, row_in_col, heading_rows=0):
    # DrawKeySettings 배치와 같은 계산(창 920×628 → 1280×720에서는 920×628)
    ww, wh = min(W - 32, 920), min(H - 32, 628)
    rx, ry = (W - ww) // 2, (H - wh) // 2
    pad, gap = 20, 18
    colw = (ww - pad * 2 - gap * 2) // 3
    x = rx + pad + col * (colw + gap) + colw - 48
    y = ry + 52 + 30 + row_in_col * 26 + 12
    return x, y


def main():
    client_dir, out = sys.argv[1], sys.argv[2]
    name = sys.argv[3] if len(sys.argv) > 3 else 'keytest'
    os.makedirs(out, exist_ok=True)
    ctl = os.path.join(client_dir, 'controls_%s.txt' % name)
    if os.path.exists(ctl):
        os.remove(ctl)
    log = open(os.path.join(out, 'client.log'), 'w')

    # ── 1회차: 키 바꾸기 ──
    p, wid = start(client_dir, name, log)
    click(wid, W // 2, H - 60)                 # 전투 HUD 판(창에 포커스 — 월드에는 아무 일도)
    key('Escape')                              # 메뉴(열린 창 · 목표가 없으면)
    shot(out, '1_menu')
    click(wid, W // 2, H // 2 - 150 + 58 + 48 + 20)   # 조작 설정
    time.sleep(0.8)
    shot(out, '2_keys')
    x, y = key_chip(0, 1)                      # 전투 칸 두 번째 줄 = 기술 1
    click(wid, x, y)
    shot(out, '3_capture')
    key('t')                                   # 기술 1 = T
    x, y = key_chip(0, 2)                      # 기술 2
    click(wid, x, y)
    key('e')                                   # E는 기술 3 — 겹침 경고
    shot(out, '4_conflict')
    ww, wh = min(W - 32, 920), min(H - 32, 628)
    rx, ry = (W - ww) // 2, (H - wh) // 2
    bar_y = ry + wh - 50 - 44 + 17
    click(wid, rx + ww - 20 - 190 + 46, bar_y)     # 맞바꾸기
    shot(out, '5_swapped')
    click(wid, rx + ww - 20 - 45, ry + wh - 50 + 19)   # 닫기
    key('Escape')
    click(wid, W // 2, H // 2 - 150 + 58 + 48 * 3 + 20)   # 게임 종료
    p.wait(timeout=30)
    print('--- controls 파일')
    print(open(ctl, encoding='utf-8').read() if os.path.exists(ctl) else '(없음)')

    # ── 2회차: 복원 · 채팅 차단 · 창 ──
    p, wid = start(client_dir, name, log)
    click(wid, W // 2, H - 60)
    shot(out, '6_restored')
    key('Return')                              # 채팅 열기
    xdo('type', '--delay', '250', 'qwer asdf')
    time.sleep(0.6)
    shot(out, '7_chat_typing')
    key('Return')                              # 보내기
    time.sleep(0.8)
    key('i')
    key('k')
    key('Tab')
    time.sleep(0.6)
    shot(out, '8_windows')
    key('Escape'); key('Escape'); key('Escape'); key('Tab')
    key('Escape')
    click(wid, W // 2, H // 2 - 150 + 58 + 48 * 3 + 20)
    try:
        p.wait(timeout=30)
    except subprocess.TimeoutExpired:
        p.kill()
    log.close()
    print(open(os.path.join(out, 'client.log'), encoding='utf-8', errors='replace').read()[-1500:])


if __name__ == '__main__':
    main()
