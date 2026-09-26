#!/usr/bin/env python3
# chat3.sh의 세 로그([chat] 줄)를 모아 채널 채팅이 기대대로 오갔는지 판정한다.
import re, sys, os
out = sys.argv[1]
logs = {}
for r in 'abc':
    p = os.path.join(out, f'c3{r}.log')
    lines = []
    if os.path.exists(p):
        for ln in open(p, encoding='utf-8', errors='replace'):
            m = re.match(r'\[chat\] t=([\d.]+) ch=(\S+) from=(\S*) text=(.*)$', ln.rstrip('\n'))
            if m: lines.append((float(m.group(1)), m.group(2), m.group(3), m.group(4)))
    logs[r] = lines

def has(r, ch, frm, text_sub):
    return any(c == ch and f == frm and text_sub in t for _, c, f, t in logs[r])
def count(r, ch, text_sub, frm=None):
    return sum(1 for _, c, f, t in logs[r] if c == ch and text_sub in t and (frm is None or f == frm))

checks = [
    ('b ← a 일반', has('b', '일반', 'c3a', '일반 대화 시험')),
    ('c ← a 일반(같은 지역)', has('c', '일반', 'c3a', '일반 대화 시험')),
    ('b ← a 전체', has('b', '전체', 'c3a', '전체 대화 시험')),
    ('c ← a 전체', has('c', '전체', 'c3a', '전체 대화 시험')),
    ('a: 10초 안 두 번째 전체는 막힘(시스템)', count('a', '시스템', '전체 대화는') >= 1 and not has('b', '전체', 'c3a', '막혀야')),
    ('b ← a 거래', has('b', '거래', 'c3a', '물약 삽니다')),
    ('c ← a 거래', has('c', '거래', 'c3a', '물약 삽니다')),
    ('b ← a 귓속말', has('b', '귓속말', 'c3a', '귓속말 시험')),
    ('c는 a→b 귓속말을 못 봄', not any('귓속말 시험' in t for _, c, f, t in logs['c'])),
    ('a: 귓속말 보냄 확인(받는 이 c3b)', has('a', '귓속말(보냄)', 'c3b', '귓속말 시험')),
    ('a ← b /r 답장', has('a', '귓속말', 'c3b', '답장 시험')),
    ('b ← a 아이템 고리({i:…})', any(c == '일반' and f == 'c3a' and '{i:' in t and '이거 어때요' in t for _, c, f, t in logs['b'])),
    ('도배: 7줄 중 5줄만(b가 받은 줄)', count('b', '일반', '도배 시험', 'c3a') == 5),
    ('도배: a에게 "말이 너무 빠릅니다" 2번', count('a', '시스템', '너무 빠릅니다') == 2),
    ('되풀이: 셋째는 막힘(b가 2번 받음)', count('b', '일반', '같은 말 되풀이 시험', 'c3a') == 2 and count('a', '시스템', '되풀이') >= 1),
    ('a ← b 일반 · c ← b 일반', has('a', '일반', 'c3b', 'b도 일반') and has('c', '일반', 'c3b', 'b도 일반')),
    ('a ← c 일반 · b ← c 일반', has('a', '일반', 'c3c', 'c입니다') and has('b', '일반', 'c3c', 'c입니다')),
]
ok = 0
for name, res in checks:
    print(f"[chat3] {'PASS' if res else 'FAIL'} {name}")
    ok += 1 if res else 0
print(f"[chat3] {ok}/{len(checks)} 통과 · 받은 줄 a={len(logs['a'])} b={len(logs['b'])} c={len(logs['c'])}")
sys.exit(0 if ok == len(checks) else 1)
