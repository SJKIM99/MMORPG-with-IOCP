# MMORPG-with-IOCP

Windows IOCP 기반 대규모 분산 MMORPG 게임 서버 · C++20 · 3개월 1인 개발

- 포트폴리오 원본 PDF — [MMORPG-with-IOCP_포트폴리오.pdf](MMORPG-with-IOCP_포트폴리오.pdf)
- 시연 영상 — [youtube.com/watch?v=BjdfZrlOad4](https://www.youtube.com/watch?v=BjdfZrlOad4)
- 2D MMORPG 클라이언트 「하르벤」 — [Client2D/README.md](Client2D/README.md) · 서버 컨텐츠 확장(종족·보스, 그로시·상점·각인 강화, 마을·사망 패널티, PK·성향) — [docs/LINEAGE_CONTENT.md](docs/LINEAGE_CONTENT.md)
- 세계관 · 이름 규칙 — [docs/WORLD.md](docs/WORLD.md) · 디자인 시스템 「새김」 — [docs/DESIGN_SYSTEM.md](docs/DESIGN_SYSTEM.md)
- 고도화 기록 — 조작 · 이동 [PHASE_1](docs/upgrade/PHASE_1.md) · 기술 시스템 · 전투 연출 [PHASE_2](docs/upgrade/PHASE_2.md) · 직업 36기술 움직이는 그림 [docs/skills](docs/skills/README.md)
- 게임 그림 · 목소리 — 캐릭터 · 몬스터 · NPC · 배경 · 소품 · 아이콘 · 효과 · UI · 대사 음성을 Higgsfield로 생성 — [tools/assetgen](tools/assetgen/README.md) · 규칙 [디자인 시스템 9장](docs/DESIGN_SYSTEM.md#9-게임-그림--higgsfield)

---

![표지](portfolio/pages/portfolio-01.png)
![목차](portfolio/pages/portfolio-02.png)
![01 프로젝트 개요](portfolio/pages/portfolio-03.png)
![02 게임 콘텐츠 — 몬스터와 아이템](portfolio/pages/portfolio-04.png)
![03 전체 아키텍처](portfolio/pages/portfolio-05.png)
![04 IOCP 네트워크 계층](portfolio/pages/portfolio-06.png)
![05 Zone 분산 — 처리량 5.5배](portfolio/pages/portfolio-07.png)
![06 Sector 시야 관리](portfolio/pages/portfolio-08.png)
![07 Lock-free 메모리 풀](portfolio/pages/portfolio-09.png)
![08 배치 송신 — Send Queue](portfolio/pages/portfolio-10.png)
![09 예약 작업 처리 — Timer Thread](portfolio/pages/portfolio-11.png)
![10 부하 테스트](portfolio/pages/portfolio-12.png)
![PART 02 인벤토리 시스템](portfolio/pages/portfolio-13.png)
![11 인벤토리 — 설계 원칙](portfolio/pages/portfolio-14.png)
![12 인벤토리 — 원자성 보장](portfolio/pages/portfolio-15.png)
![13 DB 쓰기 순서 — playerId 기반 샤딩](portfolio/pages/portfolio-16.png)
![14 필드 드롭과 루팅 우선권](portfolio/pages/portfolio-17.png)
![PART 03 완성 이후, 스스로 점검하기](portfolio/pages/portfolio-18.png)
![15 코드 리뷰로 찾아낸 결함](portfolio/pages/portfolio-19.png)
![16 검증 방식 — Negative Control](portfolio/pages/portfolio-20.png)
![17 코드 구조](portfolio/pages/portfolio-21.png)
![18 시연 영상](portfolio/pages/portfolio-22.png)
![19 회고](portfolio/pages/portfolio-23.png)

---

포트폴리오 페이지 이미지는 `portfolio/portfolio.html`을 원본으로 생성됩니다.
내용을 고친 뒤 아래를 실행하면 PDF와 위 이미지가 함께 갱신됩니다.

```
powershell -ExecutionPolicy Bypass -File portfolio\build.ps1
```
