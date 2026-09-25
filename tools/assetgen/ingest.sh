#!/bin/bash
# 사용: ingest.sh '<jobs_wait의 jobs 배열 JSON>' [다음 시작 번호] [다음 끝 번호]
# URL 기록 → 내려받기 → (선택) 다음 묶음 JSON 출력
set -e
cd "$(dirname "$0")"
RAW="${ASSETGEN_RAW:-$HOME/.cache/harven-assetgen/raw}"   # 원본 PNG 보관 폴더(저장소 밖)
python3 batch.py urls "$1"
python3 fetch.py "$RAW"
if [ -n "$2" ]; then python3 batch.py emit $2 $3; fi
