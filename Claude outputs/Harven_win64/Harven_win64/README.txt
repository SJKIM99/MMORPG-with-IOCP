하르벤 (HARVEN) - Client2D 바로 실행용 빌드 (Windows x64)

1. DB: Database/setup.sql 을 한 번 다시 실행 (그로시/성향/강화/장비/퀘스트 저장 컬럼, 기존 데이터 유지)
2. newclient 브랜치의 GameServer 를 빌드해서 실행 (포트 4000)
3. Client2D.exe 실행 -> 서버 주소 확인 -> 캐릭터 이름(영문/숫자 2~12자) + 직업 선택 -> 접속하기
   처음 쓰는 이름이면 고른 직업으로 새 캐릭터가 만들어집니다.

옵션 예: Client2D.exe --host 127.0.0.1 --port 4000 --size 1600x900
조작법은 게임 안에서 F1(도움말), 자세한 내용은 저장소의 Client2D/README.md 참고.
assets 폴더(글꼴 · 그림 · 아이콘)는 exe 옆에 있어야 합니다. 그림이 없어도 실행은 됩니다.
글꼴 사용 허가: assets/fonts/OFL-*.txt (SIL Open Font License 1.1)
