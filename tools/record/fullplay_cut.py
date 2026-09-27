#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 전체 플레이 녹화 자르기(Phase 6) — 한 번에 뜬 녹화(capture.mp4)를 README 대표 영상 한 편으로.
#
#   python3 tools/record/fullplay_cut.py <fullplay 로그> <capture.mp4> <출력 mp4> [--width 1280] [--fps 20] [--preview out.webp]
#
# 로그의 [fp] seg/mark 줄(rec=녹화 초)로 장면을 나눈다. 싸우고 · 말하고 · 사고파는 장면은 그대로,
# 걸어가는 구간(투어 뒤 훈련장까지 · 남문 밖까지 · 길잡이까지 · 회관 · 경매장까지 · 도우미가 사러 오는 동안)은 ×4로 감고
# 오른쪽 위에 "빨리 감기 ×4"를 띄운다. 잘라 낸 곳은 없다 — 처음부터 끝까지 한 세션이다.
# --preview: 앞의 장면들에서 몇 초씩 떼어 만든 짧은 WebP(README 첫 화면용).
# ─────────────────────────────────────────────────────────────────────────────
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
FONT = os.path.join(ROOT, 'Client2D', 'assets', 'fonts', 'NanumSquareRoundB.ttf')


def marks_of(log):
    m = {}
    for line in open(log, encoding='utf-8', errors='replace'):
        g = re.match(r'\[fp\] (seg \d+ (\w+)|mark ([\w-]+)) rec=([-\d.]+)', line)
        if not g:
            continue
        key = g.group(2) or g.group(3)
        t = float(g.group(4))
        if t >= 0 and key not in m:
            m[key] = t
    for line in open(log, encoding='utf-8', errors='replace'):
        d = re.match(r'\[fp\] done rec=([-\d.]+)', line)
        if d:
            m['done'] = float(d.group(1))
    return m


def plan(m, total):
    # (시작 키, 끝 키, 배속) — 키가 없으면 그 구간은 건너뛴다(다음 구간이 이어받는다)
    spec = [
        ('@0', 'tour', 1), ('tour', 'tour-end', 1), ('tour-end', 'training', 4),
        ('training', 'hunt', 1), ('hunt', 'hunt-arrive', 4), ('hunt-arrive', 'boss', 1),
        ('boss', 'boss-arrive', 4), ('boss-arrive', 'chat', 1),
        ('chat', 'guild', 1), ('guild', 'guild-npc', 4), ('guild-npc', 'auction', 1),
        ('auction', 'auction-npc', 4), ('auction-npc', 'auction-listed', 1), ('auction-listed', 'auction-sold', 3),
        ('auction-sold', 'end', 1), ('end', '@end', 1),
    ]
    at = dict(m)
    at['@0'] = 0.0
    at['@end'] = min(total, m.get('done', total) + 0.8)
    parts = []
    for a, b, speed in spec:
        if a not in at or b not in at:
            continue
        s, e = at[a], at[b]
        if e - s > 0.2:
            parts.append((s, e, speed))
    # 겹치거나 빈 곳이 없게(앞 구간의 끝 = 다음 구간의 시작)
    fixed = []
    for s, e, sp in sorted(parts):
        if fixed and s < fixed[-1][1]:
            s = fixed[-1][1]
        if fixed and s > fixed[-1][1] + 0.05:
            fixed.append((fixed[-1][1], s, 4))   # 표에 없는 틈(드묾)은 감는다
        if e - s > 0.2:
            fixed.append((s, e, sp))
    return fixed


def duration(path):
    r = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'default=nw=1:nk=1', path], capture_output=True, text=True)
    return float(r.stdout.strip() or 0)


def main():
    log, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
    width = 1280
    fps = 20
    crf = 24
    preview = None
    args = sys.argv[4:]
    for i, a in enumerate(args):
        if a == '--width':
            width = int(args[i + 1])
        if a == '--preview':
            preview = args[i + 1]
        if a == '--fps':
            fps = int(args[i + 1])
        if a == '--crf':
            crf = int(args[i + 1])
    m = marks_of(log)
    total = duration(src)
    parts = plan(m, total)
    if not parts:
        print('장면 표시가 없다:', log)
        return 1
    font = FONT.replace(':', r'\:')
    # 구간마다 따로 인코딩(입력 앞에서 찾아 들어가 메모리를 쓰지 않는다) → concat 분리기로 이어 붙인다(같은 인코딩 설정 · 다시 인코딩 없음)
    work = out + '.parts'
    os.makedirs(work, exist_ok=True)
    listing = []
    for i, (s, e, sp) in enumerate(parts):
        vf = 'setpts=(PTS-STARTPTS)/%d' % sp
        if sp > 1:
            vf += (",drawtext=fontfile='%s':text='빨리 감기 ×%d':fontcolor=0xEDE7DB:fontsize=22:borderw=2:bordercolor=0x000000@0.6:"
                   "x=w-tw-28:y=24" % (font, sp))
        vf += ',scale=%d:-2:flags=lanczos,fps=%d' % (width, fps)
        part = os.path.join(work, 'p%02d.mp4' % i)
        subprocess.run(['ffmpeg', '-y', '-v', 'error', '-ss', '%.3f' % s, '-to', '%.3f' % e, '-i', src, '-vf', vf, '-an',
                        '-c:v', 'libx264', '-preset', 'slow', '-crf', str(crf), '-pix_fmt', 'yuv420p', '-g', str(fps * 4), part], check=True)
        listing.append("file '%s'" % os.path.abspath(part))
    lst = os.path.join(work, 'list.txt')
    open(lst, 'w').write('\n'.join(listing) + '\n')
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-f', 'concat', '-safe', '0', '-i', lst, '-c', 'copy', '-movflags', '+faststart', out], check=True)
    kept = sum((e - s) / sp for s, e, sp in parts)
    print('→ %s (%.1f초 녹화 → %.1f초 영상, 구간 %d, %.1fMB)' % (out, total, kept, len(parts), os.path.getsize(out) / 1048576))
    for s, e, sp in parts:
        print('   %7.2f ~ %7.2f  ×%d' % (s, e, sp))
    if preview:
        # 첫 화면용: 장면마다 몇 초(투어 · 훈련장 · 사냥 · 보스 · 결사 · 경매) — 640px 12fps WebP
        picks = []
        for key, off, dur in (('tour', 1.0, 3.0), ('training', 6.0, 3.5), ('hunt-arrive', 3.0, 3.0), ('boss-arrive', 8.0, 4.0),
                              ('boss-down', -1.5, 3.0), ('guild-created', -0.5, 3.0), ('auction-sold', -1.0, 3.0)):
            if key in m:
                s = max(0.0, m[key] + off)
                picks.append((s, min(total, s + dur)))
        plist = []
        for i, (s, e) in enumerate(picks):
            part = os.path.join(work, 'w%02d.mp4' % i)
            subprocess.run(['ffmpeg', '-y', '-v', 'error', '-ss', '%.3f' % s, '-to', '%.3f' % e, '-i', src, '-vf', 'scale=640:-2:flags=lanczos,fps=12',
                            '-an', '-c:v', 'libx264', '-crf', '16', '-pix_fmt', 'yuv420p', part], check=True)
            plist.append("file '%s'" % os.path.abspath(part))
        pl = os.path.join(work, 'wlist.txt')
        open(pl, 'w').write('\n'.join(plist) + '\n')
        subprocess.run(['ffmpeg', '-y', '-v', 'error', '-f', 'concat', '-safe', '0', '-i', pl, '-c:v', 'libwebp_anim', '-loop', '0',
                        '-quality', '55', '-compression_level', '4', '-an', preview], check=True)
        print('→', preview, '%.1fKB' % (os.path.getsize(preview) / 1024))
    return 0


if __name__ == '__main__':
    sys.exit(main())
