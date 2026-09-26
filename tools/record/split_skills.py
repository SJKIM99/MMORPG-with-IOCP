#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# 기술별 GIF 자르기(Phase 2-E) — `--script skills:<직업> --record <폴더>`의 녹화를 기술마다 한 장씩.
#
#   split_skills.py <client.log> <capture.mp4> <출력 폴더> [폭=340] [가운데 비율=0.56] [fps=12] [형식=webp|gif]
#
#  · 스크립트가 기술이 바뀔 때마다 찍는 줄: [skillseg] t=… rec=<녹화 시작 기준 초> slot=<칸> id=<기술 id> name=<이름>
#  · 조각 = [이번 기술 rec − 0.25초, 다음 기술 rec − 0.05초](마지막은 +3초). 같은 기술이 다시 나오면(두 바퀴째) 첫 바퀴만.
#  · 파일 이름: skill_<id>.webp — 이름은 목록(index.md)에 적는다(파일 이름에 한글을 쓰지 않는다).
#  · 기본은 움직이는 WebP(같은 화질에서 GIF의 ⅓ 안팎 — 36기술을 저장소에 둘 수 있는 크기, GitHub 문서에서 바로 재생).
#    gif를 고르면 run.sh gif와 같은 2패스 팔레트.
# ─────────────────────────────────────────────────────────────────────────────
import os, re, subprocess, sys, tempfile

def main():
    if len(sys.argv) < 4:
        print(__doc__ or 'usage: split_skills.py <client.log> <capture.mp4> <out dir> [width] [crop] [fps]')
        return 2
    log, mp4, out = sys.argv[1], sys.argv[2], sys.argv[3]
    width = int(sys.argv[4]) if len(sys.argv) > 4 else 340
    crop = float(sys.argv[5]) if len(sys.argv) > 5 else 0.56
    fps = int(sys.argv[6]) if len(sys.argv) > 6 else 12
    fmt = sys.argv[7] if len(sys.argv) > 7 else 'webp'
    os.makedirs(out, exist_ok=True)
    segs = []
    pat = re.compile(r'\[skillseg\] t=[\d.]+ rec=(-?[\d.]+) slot=(\d+) id=(\d+) name=(.*)$')
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = pat.search(line.strip())
            if m and float(m.group(1)) >= 0.0:
                segs.append((float(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4)))
    if not segs:
        print('no [skillseg] lines with rec >= 0 (was --record armed before the first cast?)')
        return 1
    seen, rows = set(), []
    for i, (t, slot, sid, name) in enumerate(segs):
        if sid in seen:
            continue
        seen.add(sid)
        end = segs[i + 1][0] - 0.05 if i + 1 < len(segs) else t + 3.0
        start = max(0.0, t - 0.25)
        dur = max(0.5, end - start)
        gif = os.path.join(out, f'skill_{sid}.{fmt}')
        vf = f'crop=iw*{crop}:ih*{crop},fps={fps},scale={width}:-1:flags=lanczos'
        if fmt == 'webp':
            subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-ss', f'{start:.3f}', '-t', f'{dur:.3f}', '-i', mp4, '-vf', vf,
                            '-c:v', 'libwebp_anim', '-lossless', '0', '-q:v', '42', '-compression_level', '6', '-loop', '0', gif], check=True)
        else:
            with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as tmp:
                pal = tmp.name
            try:
                subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-ss', f'{start:.3f}', '-t', f'{dur:.3f}', '-i', mp4,
                                '-vf', f'{vf},palettegen=max_colors=128:stats_mode=diff', pal], check=True)
                subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-ss', f'{start:.3f}', '-t', f'{dur:.3f}', '-i', mp4, '-i', pal,
                                '-lavfi', f'{vf}[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle', gif], check=True)
            finally:
                os.unlink(pal)
        size = os.path.getsize(gif)
        rows.append((slot, sid, name, start, dur, size))
        print(f'{gif}  {name}  {start:.2f}s +{dur:.2f}s  {size // 1024}KB')
    keys = ['C', 'Q', 'W', 'E', 'R', 'A', 'S', 'D', 'F', 'V', 'Space', 'Z']
    with open(os.path.join(out, 'index.md'), 'w', encoding='utf-8') as f:
        f.write('| 칸 | 기술 | GIF | 길이 |\n|---|---|---|---|\n')
        for slot, sid, name, start, dur, size in rows:
            key = keys[slot] if 0 <= slot < len(keys) else str(slot)
            f.write(f'| {key} | {name} | ![{name}](skill_{sid}.{fmt}) | {dur:.1f}초 |\n')
    return 0

if __name__ == '__main__':
    sys.exit(main())
