# -*- coding: utf-8 -*-
"""
catalog.py의 요청을 Higgsfield generate_image_batch에 넣을 JSON으로 뽑고(batch.py emit),
돌려받은 job id를 jobs.json에 적는다(batch.py register).

  python3 batch.py emit 0 8            # 0~7번 요청(한 번에 8개 이하 — 동시 실행 제한)
  python3 batch.py register '<jobs json>'  # 도구 결과의 jobs 배열을 그대로 붙여 넣는다
  python3 batch.py status              # 무엇이 남았나
"""
import json
import os
import sys

from catalog import all_requests

HERE = os.path.dirname(os.path.abspath(__file__))
JOBS = os.path.join(HERE, "jobs.json")


def load():
    if os.path.exists(JOBS):
        with open(JOBS, encoding="utf-8") as f:
            return json.load(f)
    return {}


def save(db):
    with open(JOBS, "w", encoding="utf-8") as f:
        json.dump(db, f, ensure_ascii=False, indent=1, sort_keys=True)


def main():
    reqs = all_requests()
    db = load()
    cmd = sys.argv[1]
    if cmd == "emit":
        a, b = int(sys.argv[2]), int(sys.argv[3])
        out = []
        for i in range(a, min(b, len(reqs))):
            key, params = reqs[i]
            if key in db and db[key].get("job"):
                continue
            out.append({"index": i, "params": params})
        print(json.dumps(out, ensure_ascii=False))
    elif cmd == "register":
        jobs = json.loads(sys.argv[2])
        for j in jobs:
            key = reqs[j["index"]][0]
            db.setdefault(key, {})["job"] = j["job_id"]
            if j.get("result_url"):
                db[key]["url"] = j["result_url"]
        save(db)
        print("registered", len(jobs))
    elif cmd == "urls":
        # jobs_wait 결과(jobs 배열)에서 URL만 채운다
        jobs = json.loads(sys.argv[2])
        n = 0
        for j in jobs:
            key = reqs[j["index"]][0]
            if j.get("result_url"):
                db.setdefault(key, {})["url"] = j["result_url"]
                db[key]["job"] = j["job_id"]
                n += 1
            elif j.get("status") == "failed":
                db.setdefault(key, {})["failed"] = True
        save(db)
        print("urls", n)
    elif cmd == "status":
        done = sum(1 for k, _ in reqs if db.get(k, {}).get("url"))
        pend = [(i, k) for i, (k, _) in enumerate(reqs) if db.get(k, {}).get("job") and not db[k].get("url")]
        todo = [(i, k) for i, (k, _) in enumerate(reqs) if not db.get(k, {}).get("job")]
        print(f"done {done}/{len(reqs)}  pending {len(pend)}  todo {len(todo)}")
        for i, k in pend:
            print("  pending", i, k, db[k]["job"])
        print("  todo:", " ".join(f"{i}:{k}" for i, k in todo[:200]))
    elif cmd == "waitlist":
        # 아직 URL이 없는 job을 jobs_wait 입력 형식으로
        out = [{"index": i, "job_id": db[k]["job"]} for i, (k, _) in enumerate(reqs)
               if db.get(k, {}).get("job") and not db[k].get("url")]
        print(json.dumps(out[:12]))


if __name__ == "__main__":
    main()
