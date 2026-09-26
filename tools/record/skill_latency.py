#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 예측 타격 ↔ 서버 피해 간격(Phase 2 완료 기준 "임팩트와 피해 숫자 차이 2프레임 이내 · 지연에서도 순서가 뒤집히지 않는다")
#
#   skill_latency.py <client.log> [...]     (HARVEN_SKILL_TRACE=1로 뜬 클라 로그)
#
#  · 표의 타격(피해 hit ≥ 128 = 0x80 | 줄 번호)마다, 같은 기술 · 같은 줄의 바로 앞 예측 타격(beat)을 찾는다(1.5초 안).
#  · 간격 = 프레임 수(f=) · 초(t=). 음수 = 피해가 예측보다 먼저 = 순서 뒤집힘.
#  · 예측 타격이 없는 피해(모양 밖으로 예측했는데 맞음 · 장판 틱 · 예전 판정 기술)는 따로 센다.
# ─────────────────────────────────────────────────────────────────────────────
import re, sys, statistics
beat_re = re.compile(r'\[skill\] t=([\d.]+) beat id=(\d+) hit=(\d+) stage=\d+ targets=(\d+) f=(\d+)')
dmg_re = re.compile(r'\[skill\] t=([\d.]+) dmg id=(\d+) hit=(\d+) .* f=(\d+)')
for path in sys.argv[1:]:
    beats, dmgs = {}, []
    for line in open(path, encoding='utf-8', errors='replace'):
        m = beat_re.search(line)
        if m:
            t, sid, hit, f = float(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(5))
            beats.setdefault((sid, hit), []).append((t, f))
            continue
        m = dmg_re.search(line)
        if m and 'flags2=' in line:
            f2 = int(re.search(r'flags2=(\d+)', line).group(1))
            dmgs.append((float(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), f2))
    rows, orphan, legacy, procs = [], 0, 0, 0
    for t, sid, hit, f, f2 in dmgs:
        if hit < 128:
            legacy += 1            # 예전 판정 기술(표의 타격 번호가 없다)
            continue
        if f2 & 0x40:              # DAMAGE2_PROC(잔향 · 분신이 따라 한 몫) — 예측 타격이 없다
            procs += 1
            continue
        near = [b for b in beats.get((sid, hit - 128), []) if t - 1.5 <= b[0] <= t + 0.5]
        if not near:
            orphan += 1
            continue
        # 피해보다 앞선(같은 프레임 포함) 예측 중 가장 늦은 것 — 없으면 뒤의 것(= 순서 뒤집힘). 틱(0.25초마다)이 다음 틱과 잘못 짝지어지지 않게
        before = [x for x in near if x[1] <= f]
        b = max(before, key=lambda x: x[0]) if before else min(near, key=lambda x: x[0])
        rows.append((f - b[1], t - b[0], sid))
    if not rows:
        print(path, 'no matched hits'); continue
    fr = [r[0] for r in rows]
    sec = [r[1] for r in rows]
    inv = sum(1 for x in fr if x < 0)
    fr_s = sorted(fr)
    print(f'{path}: 맞춘 타격 {len(rows)} · 예측 없는 피해 {orphan} · 예전 판정 {legacy} · 따라 한 몫 {procs}')
    print(f'  프레임 차이(피해 − 예측) — 중앙 {statistics.median(fr):.0f} · 90% {fr_s[max(0, int(len(fr) * 0.9) - 1)]} · 최대 {max(fr)} · 최소 {min(fr)}'
          f' · 0~2프레임 {sum(1 for x in fr if 0 <= x <= 2)}/{len(fr)} · 뒤집힘(피해가 먼저) {inv}')
    print(f'  초 차이 — 중앙 {statistics.median(sec) * 1000:.0f}ms · 최대 {max(sec) * 1000:.0f}ms · 최소 {min(sec) * 1000:.0f}ms')
