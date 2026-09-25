# -*- coding: utf-8 -*-
"""jobs.json의 URL을 raw 폴더로 내려받는다(이미 있으면 건너뛴다).  python3 fetch.py <raw 폴더>"""
import json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
raw = sys.argv[1]
os.makedirs(raw, exist_ok=True)
db = json.load(open(os.path.join(HERE, "jobs.json"), encoding="utf-8"))
n = 0
for k, v in sorted(db.items()):
    p = os.path.join(raw, k + ".png")
    if v.get("url") and not os.path.exists(p):
        subprocess.run(["curl", "-sS", "--retry", "3", "-o", p, v["url"]], check=True)
        n += 1
print("downloaded", n)
