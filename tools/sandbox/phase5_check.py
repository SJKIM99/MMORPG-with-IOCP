#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# Phase 5 시험 판정 — tools/sandbox/phase5.sh의 출력 폴더(클라 로그 · 서버 로그)를 읽는다.
#   python3 tools/sandbox/phase5_check.py <출력 폴더>
# 있는 시험만 판정한다. 줄마다 PASS/FAIL, 끝에 합계.
# ─────────────────────────────────────────────────────────────────────────────
import os
import re
import sys

OUT = sys.argv[1]
results = []


def check(ok, what):
    results.append((bool(ok), what))
    print(('PASS ' if ok else 'FAIL ') + what)


def read(name):
    p = os.path.join(OUT, name)
    if not os.path.exists(p):
        return None
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()


def acks(log, kind):
    # [guild] ack op=2 result=0 seq=3 param=1 / [auction] ack op=2 result=0 seq=5 ...
    return [(int(m.group(1)), int(m.group(2)), int(m.group(3))) for m in re.finditer(r'\[%s\] ack op=(\d+) result=(\d+) seq=(\d+)' % kind, log or '')]


def audit(log):
    m = re.search(r'\[Audit\] adena wallet=(\d+) settle=(\d+) sink=(\d+) total=(\d+) \| items inv=(\d+) listing=(\d+) settle=(\d+) total=(\d+)', log or '')
    return tuple(int(x) for x in m.groups()) if m else None


# ── 결사(클라 셋) ──
for tag in ('guild3', 'guild3rec'):
    a, b, c = read('lv20_ga.log'), read('lv20_gb.log'), read('lv20_gc.log')
    srv = read(tag + '_server.log')
    if srv is None or a is None:
        continue
    print('── %s ──' % tag)
    ga = acks(a, 'guild')
    check(any(op == 2 and r == 0 for op, r, s in ga), 'a: 결사 세우기 Ok(GUILD_ACK Create)')
    check('[Guild] created id=' in srv, '서버: [Guild] created')
    check(any(op == 3 and r == 15 for op, r, s in ga), 'a: 같은 순번의 초대 두 번째 → Duplicate(15)')
    invites_ok = [s for op, r, s in ga if op == 3 and r == 0]
    check(len(invites_ok) >= 3, 'a: 초대 Ok 세 번(b · c 거절 · c) — %d' % len(invites_ok))
    check('[guild] nfy ev=12' in a, 'a: c의 거절 알림(InviteDeclined)')
    check(re.search(r'\[g3\] done PASS members=2 bElite=1 kicked=1', a) is not None, 'a: [g3] done PASS(b 정예 · c 추방 → 구성원 2)')
    check(any(op == 6 and r == 0 for op, r, s in ga), 'a: 추방 Ok(GUILD_ACK Kick)')
    for who, log in (('b', b), ('c', c)):
        if log is None:
            check(False, '%s 로그 없음' % who)
            continue
        replies = [(op, r) for op, r, s in acks(log, 'guild') if op == 4]
        check(any(r == 0 for op, r in replies), '%s: 초대에 답 Ok' % who)
        check(re.search(r'\[g3\] joined id=\d+ name=새벽서약', log) is not None, '%s: 결사에 들어감' % who)
        check(re.search(r'\[guild\] nfy ev=10 who= guild=[1-9]', log) is not None, '%s: 머리 위 표시(GUILD_NFY Tag) 받음' % who)
    if c is not None:
        check(len(re.findall(r'\[g3\] invite #\d+', c)) >= 2 and 'decline' in c and 'accept' in c, 'c: 첫 초대 거절 → 두 번째 수락')
        check('[guild] nfy ev=3 who=lv20_gc' in c and '[g3] c kicked id=0' in c, 'c: 추방 알림 → 결사 없음')
    # 결사 채팅: 셋 모두 a의 말을 받았다(SC_CHAT_EX 결사 채널 — 클라 채팅 기록은 로그에 없으니 서버 · 흐름으로)
    check('[g3] c joined members=3' in a, 'a: c까지 들어온 뒤 구성원 3')

# ── 경매: 판매 · 구매 · 정산 ──
s_log, b_log, srv = read('lv20_ak_s.log'), read('lv20_ab.log'), read('auction_server.log')
if s_log and b_log and srv:
    print('── auction ──')
    sa = acks(s_log, 'auction')
    check(len([1 for op, r, s in sa if op == 1 and r == 0]) == 3, '판매자: 올리기 Ok 세 번')
    check(re.search(r'\[auc\] buy result=0', b_log) is not None, '구매자: 사기 Ok')
    check('[Auction] sold listing=' in srv, '서버: [Auction] sold')
    m0 = re.search(r'\[auc\] buyer adena0=(\d+)', b_log)
    m1 = re.search(r'\[auc\] buy result=0 adena=(\d+)', b_log)
    if m0 and m1:
        check(int(m0.group(1)) - int(m1.group(1)) == 12000, '구매자: 그로시 -12,000(값 그대로) — %s → %s' % (m0.group(1), m1.group(1)))
    check(re.search(r'\[auc\] buy done won item=3 have=1', b_log) is not None, '구매자: 정산함에서 철검을 받음(가방에 1)')
    ms0 = re.search(r'\[auc\] seller adena0=(\d+)', s_log)
    ms1 = re.search(r'\[auc\] seller listed adena=(\d+)', s_log)
    ms2 = re.search(r'\[auc\] seller done adena=(\d+)', s_log)
    if ms0 and ms1:
        dep = 120 + 300 + 6   # 보증금 1%: 12,000 · 30,000 · 600
        check(int(ms0.group(1)) - int(ms1.group(1)) == dep, '판매자: 보증금 합 %d — %s → %s' % (dep, ms0.group(1), ms1.group(1)))
    if ms1 and ms2:
        check(int(ms2.group(1)) - int(ms1.group(1)) == 12000 - 600, '판매자: 정산 받기 +11,400(수수료 5%% 뺌) — %s → %s' % (ms1.group(1), ms2.group(1)))

# ── 경합 ──
r1, r2, srv = read('lv20_r1.log'), read('lv20_r2.log'), read('race_server.log')
if r1 and r2 and srv:
    print('── race ──')
    won = [n for n, l in (('r1', r1), ('r2', r2)) if '[auc] race done won' in l]
    lost = [n for n, l in (('r1', r1), ('r2', r2)) if '[auc] race done lost' in l]
    check(len(won) == 1 and len(lost) == 1, '한 명만 산다 — 산 쪽 %s · 못 산 쪽 %s' % (won, lost))
    check(len(re.findall(r'\[Auction\] sold listing=', srv)) == 1, '서버: 팔림 한 번')
    for n, l in (('r1', r1), ('r2', r2)):
        if '[auc] race done lost' in l:
            check('unchanged=1' in l, '%s(못 산 쪽): 그로시 그대로' % n)
            m = re.search(r'\[auc\] buy result=(\d+)', l)
            check(m is not None and int(m.group(1)) in (1, 11), '%s: 결과 NotFound(1) 또는 Busy(11 — 다른 사람이 사는 중) — %s' % (n, m.group(1) if m else '?'))
    ts = re.findall(r'\[auc\] race sent at=(\d+)', r1 + r2)
    if len(ts) == 2:
        check(abs(int(ts[0]) - int(ts[1])) < 400, '두 요청 사이 %d ms(같은 순간)' % abs(int(ts[0]) - int(ts[1])))

# ── 같은 순번 ──
d, srv = read('lv20_dd.log'), read('dup_server.log')
if d and srv:
    print('── dup ──')
    da = acks(d, 'auction')
    buys = [(r, s) for op, r, s in da if op == 2]
    seqs = {s for r, s in buys}
    check(len(buys) == 2 and len(seqs) == 1, '같은 순번 두 요청 → 결과 두 개 — %s' % buys)
    check(sorted(r for r, s in buys) == [0, 13], '하나는 Ok(0) · 하나는 Duplicate(13)')
    check(len(re.findall(r'\[Auction\] sold listing=', srv)) == 1, '서버: 팔림 한 번')

# ── 만료 ──
e, srv = read('lv20_ak_e.log'), read('expire_server.log')
if e and srv:
    print('── expire ──')
    check('hourSec=2' in srv, '서버: 한 시간 = 2초')
    check('[Auction] expired listing=' in srv, '서버: [Auction] expired')
    check(re.search(r'\[auction\] nfy ev=1 ', e) is not None, '판매자: 만료 알림(AUCTION_NFY Expired)')
    check(re.search(r'\[auction\] nfy ev=(1|2) id=\d+ item=\d+ price=\d+ settle=1', e) is not None, '판매자: 정산함 1줄')

# ── 장애 ──
c1, c2, c3, c4, c5 = (read('crash_%s.log' % n) for n in ('1_register', '2_before_kill', '3_restart', '4_buy', '5_final'))
if c2 and c3:
    print('── crash ──')
    a2, a3 = audit(c2), audit(c3)
    check(a2 is not None and a3 is not None, '켤 때마다 [Audit] 한 줄')
    check('AuctionBuy listing=' in c2 and '멈춤' in c2, '② 구매 트랜잭션 안에서 멈춤 — 그 사이 서버를 죽였다')
    check('[Auction] sold' not in c2, '② 죽이기 전에 팔림 확정 없음')
    check('replayed=' in c3, '③ 일지를 다시 적용했다')
    if a2 and a3:
        check(a2[3] == a3[3], '③ 그로시 총량 보존(지갑 + 대금 + 보증금 · 수수료) %d → %d' % (a2[3], a3[3]))
        check(a2[7] == a3[7], '③ 물건 총량 보존(소지품 + 판매 중 + 정산함) %d → %d' % (a2[7], a3[7]))
        check(a3[5] == 1, '③ 목록은 판매 중으로 남았다(listing units %d)' % a3[5])
    m = re.search(r'\[Phase5\] guilds=\d+ members=\d+ listings=(\d+)', c3)
    check(m is not None and int(m.group(1)) == 1, '③ 서비스가 판매 중 목록 1개를 불러왔다')
    if c4 and c5:
        a5 = audit(c5)
        check('[Auction] sold listing=' in c4, '④ 다시 산다 → 팔림')
        if a3 and a5:
            check(a3[3] == a5[3], '⑤ 그로시 총량 보존 %d → %d' % (a3[3], a5[3]))
            check(a3[7] == a5[7], '⑤ 물건 총량 보존 %d → %d' % (a3[7], a5[7]))
            check(a5[5] == 0 and a5[6] == 1, '⑤ 목록 0 · 정산함 물건 1(구매자가 받기 전)')

ok = sum(1 for r, _ in results if r)
print('RESULT %d/%d' % (ok, len(results)))
