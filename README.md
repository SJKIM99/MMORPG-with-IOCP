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

---

## 3D 에셋 받기

3D 에셋 원본 255MB는 저장소에 포함되어 있지 않습니다 (`.gitignore`). 전부 **CC0**이며
아래에서 **0원**으로 받을 수 있습니다. itch.io는 name-your-own-price이므로 금액 입력란에
`0`을 넣고 *No thanks, just take me to the downloads* 를 누르면 됩니다.

받은 zip을 아래 경로에 **폴더째** 풀어주세요. 경로가 곧 `world/manifest.json`의
`sources.*.root`이므로 이름이 정확해야 합니다.

| 받을 곳 | 압축 해제 위치 | 용도 |
|---|---|---|
| [KayKit Skeletons 1.1](https://kaylousberg.itch.io/kaykit-skeletons) | `assets/vendor/kaykit_skeletons/` | 몬스터 3종 |
| [KayKit Adventurers 2.0](https://kaylousberg.itch.io/kaykit-adventurers) | `assets/vendor/kaykit_adventurers/` | 플레이어, 상인 NPC |
| [KayKit Dungeon Pack 1.1](https://kaylousberg.itch.io/dungeon-remastered-pack) | `assets/vendor/kaykit_dungeon/` | 인던 128² 모듈 |
| [KayKit Medieval Builder Pack 1.0](https://kaylousberg.itch.io/medieval-builder-pack) | `assets/vendor/kaykit_medieval/` | 마을 256² 건물 |
| [Quaternius Stylized Nature MegaKit](https://quaternius.com/packs/stylizednaturemegakit.html) | `assets/vendor/quaternius_nature/` | 필드 512² 식생·바위 |

압축 해제 후 아래로 경로를 검증합니다. `manifest.json`에 등록된 51개 에셋과
외부 `.bin`/`.png` 의존성이 전부 존재하는지 확인합니다.

```
powershell -ExecutionPolicy Bypass -File tools\verify_assets.ps1
```

### 클론 직후 — 에셋 정션 연결

Godot은 프로젝트 폴더 아래(`res://`)만 봅니다. 에셋은 `assets/vendor/`에 있고
Godot 프로젝트는 `Client/`에 있으므로 디렉터리 정션으로 이어줍니다.
**정션은 git에 담기지 않으므로 클론할 때마다 한 번 만들어야 합니다.**

`mklink`는 cmd 내장 명령이라 **PowerShell이 아닌 cmd**에서 실행합니다.
관리자 권한은 필요 없습니다.

```
cd /d <저장소 경로>
mklink /J Client\assets assets\vendor
```

연결되면 Godot FileSystem 독에 `res://assets/` 아래로 5개 팩이 보입니다.
첫 임포트는 255MB라 수 분 걸립니다.

### 주의

- **`Asset/` 루트에 한꺼번에 풀지 마세요.** KayKit 팩 중 일부는 zip 안에
  `License.txt`, `Models/`를 최상위에 두고 있어, 같은 폴더에 여러 팩을 풀면
  서로 덮어씁니다. 실제로 Medieval Builder Pack이 Character Animations의
  `License.txt`를 덮어쓴 이력이 있습니다 (`assets/vendor/kaykit_animations/LICENSE_NOTE.md`).
- **glTF(`.glb` / `.gltf`)만 사용합니다.** 각 팩의 `FBX/` 폴더는 쓰지 않습니다.
- 각 에셋의 ID·콜리전 타입·출처·라이선스는 전부 [`world/manifest.json`](world/manifest.json)에
  기록되어 있습니다. 씬에 손으로 배치하지 않고 이 파일과 `world/regions/*.json`을
  서버와 클라이언트가 함께 읽습니다.
