# MMORPG-with-IOCP

Windows IOCP 기반 대규모 분산 MMORPG 게임 서버 · C++20 · 3개월 1인 개발

- 포트폴리오 원본 PDF — [MMORPG-with-IOCP_포트폴리오.pdf](MMORPG-with-IOCP_포트폴리오.pdf)
- 시연 영상 — [youtube.com/watch?v=BjdfZrlOad4](https://www.youtube.com/watch?v=BjdfZrlOad4)

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
