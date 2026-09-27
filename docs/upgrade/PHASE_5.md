# Phase 5 — 결사(길드) · 경매장 — 서버 + DB + 클라

> 작업 지시: `claude/UPGRADE_PROMPT.md` [Phase 5]. 진단 · 설계 메모이며 하위 단계를 끝낼 때마다 결과를 덧붙인다.
> 코드 기준: Phase 4 끝(`newclient` `2fb78fe` + 문서 커밋). 결정 기록: [`docs/adr/0001-guild-auction-services.md`](../adr/0001-guild-auction-services.md).

## 0. 기준 — 지금 코드

| 항목 | 지금 | Phase 5에 필요한 것 |
|---|---|---|
| 스레드 | Zone(4×4 = 16)마다 `GameLogicThread` 하나(작업 큐). 패킷은 `WorkerThread`가 `EnqueueBySession`으로 **그 세션의 Zone 스레드**에 넘기고 `Route::Dispatch`가 거기서 돈다. 플레이어 상태(그로시 · 소지품)는 소유 Zone 스레드만 바꾼다(`ASSERT_OWNED_BY_CURRENT_ZONE`) | 여러 Zone에 걸친 전역 상태(결사 · 경매 목록)의 주인 |
| DB | `DBThread` 샤드 8개 — 같은 playerId의 쓰기는 한 샤드가 넣은 순서대로. 이벤트는 결과를 돌려주지 않는다(로그인만 Zone 작업으로 되돌림). `ShopTransaction` = 그로시 + 슬롯 하나를 한 트랜잭션 | 결과가 필요한 트랜잭션(경매 구매 · 등록 · 정산 받기 · 결사 세우기) |
| 소지품 슬롯 값 | itemId · count · equipped · enchant · extra(BIGINT — 희귀도 · 옵션 4줄 · 특수 효과, `Data/ItemRules.h`) | 경매 목록 · 정산함 행이 같은 값을 그대로 보존 |
| 이름 → 세션 | `GameSessionManager::FindByName`(Phase 3 귓속말) | 결사 초대 · 결사원 접속 알림 |
| 패킷 | `PacketType` 89개(0~88) — 127까지 38개 | 결사 · 경매 묶음 패킷(요청 + `op`) |
| 클라 자리 | 채팅 결사 채널 · `/길` · `/초대` · 이름 메뉴 "결사 초대" · 메뉴 막대 결사/경매장 칸(지금 "아직 문을 열지 않았다") · 정산함 점 자리 · 결사 회관(즈데노) · 경매장(로렌초) 건물 · 초상화 | 창 · 초대 알림 · 머리 위 문장 |
| 샌드박스 DB | `tools/sandbox/MemDBConnection.cpp` — 메모리 맵, 한 호출 = 한 락 = 한 트랜잭션. **꺼지면 사라진다** | 장애 시험(구매 도중 강제 종료 → 재시작 후 총량 보존)을 하려면 커밋 기록(일지)이 필요 |

**기준선**(바꾸기 전): 메뉴 막대의 결사 · 경매장 칸(아직 열지 않음) · 결사 회관지기 즈데노 · 경매인 로렌초와의 대화 — `img/phase5_before_*.jpg`.

## 1. 설계 — 전역 서비스(액터) + 정산함

| 결정 | 고른 것 | 대안 | 이유 |
|---|---|---|---|
| 결사 · 경매 상태의 주인 | **전용 단일 스레드 서비스** `GuildService` · `AuctionService` — 각자 `GameLogicThread` 하나(Zone과 같은 작업 큐)로 자기 상태를 소유. Zone ↔ 서비스는 큐 메시지로만 | 전역 뮤텍스 / 경매를 한 Zone에 | 락 없이 순서가 정해진다(한 목록을 두 사람이 동시에 사도 서비스가 한 줄로 받는다). Zone 스레드 모델을 깨지 않는다 |
| 경매 구매 순서 | ① 구매자 Zone → 서비스: 사고 싶다(목록 · 값) ② 서비스: 판매 중이면 **잠금**(거래 중) → Zone에 "잡았다" ③ Zone: 그로시 확인 · 메모리에서 차감 → 구매자 샤드에 **한 트랜잭션**(목록 판매 중 확인 · 구매자 그로시 · 구매자 물건 정산 행 · 판매자 대금 정산 행(수수료 뺀 값) · 시세 기록) ④ DB → 서비스: 확정 → 판매자 · 구매자 알림 | 지시서 권장(Zone이 먼저 예약 차감) | 이미 팔린 목록은 ②에서 걸러져 **진 쪽은 그로시가 한 번도 빠지지 않는다**(되돌림 창이 없다). ③의 트랜잭션은 서비스가 잠근 목록이라 정상 경로에서는 실패하지 않는다 |
| 대금 · 산 물건 | **정산함**(행) — 판매자 지갑에 직접 넣지 않고, 산 물건도 소지품이 아니라 정산함으로 | 판매자 지갑에 바로 | 그로시 · 소지품의 작성자는 소유 Zone 하나뿐 — 판매자가 오프라인이거나 다른 Zone이어도 "메모리 값이 DB를 덮어쓰는 잃어버린 갱신"이 없다. 구매 트랜잭션이 **구매자 샤드 하나**에서 끝난다 |
| 등록 | 판매자 Zone → 서비스: 자리(1인 10개) · 목록 번호 예약 → Zone: 슬롯에서 빼고 보증금 차감 → 판매자 샤드 한 트랜잭션(슬롯 · 그로시 · 목록 행) → 서비스: 목록 공개 | DB가 번호 발급 | 번호를 서비스가 정해(시작할 때 DB 최댓값 + 1) 비동기 경로에서 DB 반환값을 기다리지 않는다 |
| 취소 · 만료 | 서비스가 목록을 잠그고 판매자 샤드 한 트랜잭션(목록 상태 · 정산함 물건 행). 만료는 1분마다 서비스 틱 | | 목록 행과 정산함 행이 함께 바뀐다 |
| 정산함 받기 | 행 하나씩 — Zone이 자리 확인 → 메모리 반영 → 받는 사람 샤드 한 트랜잭션(행 받음 표시 · 그로시 · 슬롯) → 서비스가 행 지움. "모두 받기" = 한 줄씩 이어서 | 여러 행 한 번에(표 값 인자) | ODBC 표 값 인자 없이 프로시저 인자 몇 개로 끝난다. 행마다 원자적 |
| 멱등(`seq`) | 요청마다 순번 — 소유 Zone이 사람마다 마지막 순번을 기억해 같은 순번은 한 번만. 목록 상태 검사(판매 중일 때만)가 DB 쪽 두 번째 문 | DB 멱등 표 | 재접속하면 클라 순번이 다시 1부터라 DB 표는 오히려 막는다. 같은 목록은 두 번 팔릴 수 없다(상태 검사) |
| DB 샤드 | 돈 · 물건이 움직이는 트랜잭션은 **그 돈 · 물건 주인의 샤드**(구매 = 구매자, 등록 · 취소 · 만료 = 판매자, 받기 = 받는 사람). 결사 행은 결사 번호 샤드 | | 한 사람의 그로시 쓰기 순서(절대값 저장)가 트랜잭션과 섞여도 넣은 순서 그대로 |
| 결과 돌려받기 | 새 DB 이벤트에 **완료 콜백**(DB 스레드가 부르고, 콜백은 서비스 큐에 작업을 넣는다) | | 기존 이벤트(결과 없음)는 그대로 |
| 서버 재시작 | 서비스가 켤 때 DB에서 판매 중 목록 · 받지 않은 정산함 · 결사 · 결사원을 읽는다(메인 스레드, 접속을 받기 전) | | |
| 패킷 | 묶음: `GUILD_REQ`(C→S, `op`) · `GUILD_ACK` · `GUILD_INF`(정보 · 구성원 페이지 · 이름) · `GUILD_NFY`(초대 · 드나듦 · 직위 · 머리 위 표시) · `AUCTION_REQ` · `AUCTION_ACK` · `AUCTION_LIST_INF`(검색 · 내 경매 · 정산함 페이지) · `AUCTION_NFY`(팔림 · 정산함 점) — 8개 → 97개(0~96), 127까지 30개 | 기능마다 패킷 | 번호를 아낀다(지시서: 127에 가까워지면 묶음) |
| 장애 시험(샌드박스) | 인메모리 DB에 **커밋 일지**(한 호출 = 한 줄, 쓴 뒤 fsync — 켤 때 다시 적용) + 구매 트랜잭션 안에서 잠깐 멈추는 시험 스위치 → 그 사이 서버를 죽이고 다시 켜서 총량 비교 | | 실제 MSSQL의 원자성은 "실기 확인 필요", 서버 쪽 순서 · 복구 논리는 여기서 시험한다 |

## 2. 규칙(공유 헤더 `Data/GuildAuction.h`)

| 결사 | 값 |
|---|---|
| 세우기 | 레벨 15 이상 · 10,000 그로시 · 이름 2~8자(한글 · 영문 · 숫자, 띄어쓰기 · 금칙어 없음, 대소문자 무시하고 겹치지 않게) · 문장(모양 12 × 색 6) |
| 인원 | 30명 |
| 직위 | **결사장** · **부관** · **정예** · **결사원**(WORLD.md 등록) |
| 권한 | 초대: 정예 이상 / 추방: 부관 이상(자기보다 아래만) / 직위 바꾸기: 부관(정예까지) · 결사장 / 결사장 넘기기 · 해산 · 문장: 결사장 / 공지: 부관 이상 |
| 초대 | 30초 안에 수락 · 거절(받는 사람마다 하나) |

| 경매장 | 값 |
|---|---|
| 1인 동시 등록 | 10개 |
| 기간 | 12 · 24 · 48시간 |
| 보증금 | 가격의 1%(최소 1) — 돌려주지 않는다 |
| 수수료 | 팔리면 5%(최소 1) — 판매자 정산은 가격 − 수수료 |
| 가격 | 10 ~ 100,000,000 그로시 |
| 올릴 수 없는 것 | 착용 중인 장비 · 퀘스트 물건 |
| 검색 | 서비스 메모리 색인(분류 · 이름 · 희귀도 · 강화 · 착용 레벨 · 옵션) — 페이지당 6줄 |

## 3. 진행 순서 · 커밋

5-A 서비스 뼈대 · 패킷 · DB(setup.sql · 인메모리 DB · 일지) · ADR → 5-B 결사(서버 · 창 · 머리 위 문장 · 결사 채팅 · 클라 셋 녹화) →
5-C 경매장(서버 · 창 · 정산함 · 경합 · 멱등 · 장애 시험 · 녹화) → 자기 검토 · 부하 · 회귀 · 문서. 커밋 `feat(guild)` · `feat(auction)`.

## 4. 구현

### 4.1 서버 — 스레드 · 흐름

| 조각 | 파일 | 누가 · 어느 스레드 |
|---|---|---|
| 서비스 공용 | `Content/ServiceCommon.h` | `Service::Ctx`(요청한 사람의 사본 — playerId · ObjID · 세션 약한 참조 · 이름 · 레벨 · 직업) · `PostToOwner(obj, fn)`(그 사람을 소유한 Zone에서 실행 — Zone을 옮기는 중이면 새 Zone 입장 뒤로 다시 넘긴다 · 나갔으면 `nullptr`) · `SendTo`(세션 송신 락만) |
| 결사 | `Content/GuildService.{h,cpp}` | `GuildService`(전용 스레드 — 결사 · 결사원 · 초대 · 이름 겹침) + `GuildHelper`(요청한 사람의 Zone — 검사 · 멱등 · 머리 위 표시 · 결사 채팅 넘기기) |
| 경매 | `Content/AuctionService.{h,cpp}` | `AuctionService`(전용 스레드 — 판매 중 목록 · 분류 색인 · 받지 않은 정산함 · 시세 20건) + `AuctionHelper`(Zone — 검사 · 멱등 · 경매인 거리) |
| 결과 있는 트랜잭션 | `Thread/DBThread` `RequestTx(playerId, …)` · `RequestTxKeyed(key, …)` | 새 이벤트 `DB_TX_EVENT{ work, done }` — DB 스레드가 프로시저 하나를 부르고 `done(ok)`로 서비스 · Zone 큐에 넣기만 한다(기존 이벤트는 그대로) |
| 메모리만 바꾸는 소지품 함수 | `Item/Inventory` `ReserveAdena` · `ReturnAdena` · `CreditAdenaNoSave` · `TakeForListing` · `RestoreTaken` · `FindSlotForIncoming` · `PlaceIncoming` · `UndoIncoming` | 소유 Zone만. 저장은 프로시저 하나가 그로시(절대값) · 슬롯을 함께 커밋 — 실패하면 `Return*` · `Restore*` · `Undo*`가 되돌리고 그때 저장 |
| 켤 때 | `GameServer.cpp` | 워커 스레드(접속)를 띄우기 전 메인 스레드: `AuctionMaxIds` → 두 서비스 `LoadFromDB` → `[Phase5]` · `[Audit]` 한 줄씩 → 서비스 스레드 둘 + 1초 틱 스레드. DB를 못 읽으면 서비스를 끈다(요청에는 Busy) |

**결사 변경은 DB 먼저, 커밋되면 메모리**(메모리와 DB가 어긋나지 않는다). 세우기는 그로시와 한 트랜잭션이라 **만드는 사람 샤드**, 나머지는 **결사 번호 샤드**
(한 결사의 변경이 넣은 순서대로). 샤드가 둘로 갈리므로 **한 사람의 결사 변경(세우기 · 들어옴 · 나감 · 추방 · 해산)은 한 번에 하나**(`_busyPlayers`) —
떠난 행이 지워지기 전에 새 결사 행이 먼저 커밋되는 순서 뒤집힘이 없다. 한 결사의 구조 변경(추방 · 직위 · 넘기기 · 공지 · 문장 · 해산)도 한 번에 하나(`Guild::busy`),
들어옴은 여러 건을 함께(인원 한도에 `pendingJoins`를 센다). 겹치면 `Busy` — 클라는 "잠시 뒤에".

**머리 위 표시**: `User::m_guildId` · `m_guildEmblem`(atomic — 시야 경계의 다른 Zone도 읽는다). 서비스가 바꿀 때는 그 사람의 Zone으로 넘겨(`ApplyTag`) 본인 · 주변 확장 세션에
`GUILD_NFY Tag`. 시야에 들어올 때는 `SUBJECT_ADD` 바로 뒤에 한 번(결사가 있을 때만 — 레거시 · 봇은 받지 않는다). 이름은 **클라 캐시** — 서버가 읽기 전용 이름 표
(`shared_mutex`, 세우기 · 문장 · 해산 때만 씀)에서 찾아 실어 보내고, 없으면 클라가 `GuildOp::Name`으로 결사 번호마다 한 번 묻는다(패킷이 고정 크기라 이름을 실어도 바이트는 같다).

**경매 거래 거리**: 올리기 · 사기 · 내리기 · 받기는 **경매인 로렌초 곁**(`NpcHelper::IsNear` — 상점과 같은 규칙), 검색 · 내 경매 · 정산함 보기는 어디서나.
세우기 · 문장은 **회관지기 즈데노 곁**.

**검색**: 서비스 메모리 — 분류마다 목록 번호 집합(`_byCat`)을 두고 고른 분류만 훑는다(전체면 모두). 거르기: 세부 분류 · 희귀도(`EffectiveRarity`) · 각인 · 착용 레벨 · 옵션 포함 · 이름(표시 이름
UTF-8 부분 문자열). 정렬: 한 개 값 오름 · 내림 · 끝나는 순 · 희귀한 순. 한 페이지 6줄. 검색 · 목록 요청은 사람마다 0.12초에 한 번(넘치면 버린다).

**만료**: 1초 틱이 서비스 큐에 `Tick` — 경매는 1분마다(`kExpireTickMs`), 시험 배율(`HARVEN_AUCTION_HOUR_SEC` — 한 시간을 몇 초로)이면 "게임 1분"마다(최소 1초) 판매 중 · 기한 지난 목록을
잠그고 판매자 샤드 `AuctionClose(3)` → 정산함. 한 번에 64개까지.

### 4.2 DB(`Database/setup.sql` 5 · 6장 — `IF NOT EXISTS`, 기존 표 · 프로시저 그대로)

| 표 | 열(요약) | 비고 |
|---|---|---|
| `Guilds` | guildId(서비스가 정함) · name · nameKey(UNIQUE — 소문자) · emblem · notice · leaderId · createdAt | 이름 겹침의 뒷문 |
| `GuildMembers` | playerId(PK — 한 사람 한 결사) · guildId(FK, ON DELETE CASCADE) · rank(CHECK 0~3) · lastSeen | |
| `AuctionListings` | listingId · sellerId · itemId · count · enchant · extra(희귀도 · 옵션 BIGINT) · price · deposit · createdAt · expiresAt · status(0 판매 중 · 1 팔림 · 2 취소 · 3 만료) · buyerId · closedAt | 인덱스(status, expiresAt) · (sellerId, status) |
| `AuctionSettlements` | settlementId · playerId · kind(1 그로시 · 2 물건) · adena · 물건 값들 · listingId · reason · createdAt · claimedAt | 받지 않은 행 = claimedAt NULL |
| `AuctionPriceHistory` | itemId · 한 개 값 · soldAt | 켤 때 최근 14일 → 품목마다 20건 |

프로시저(모두 한 트랜잭션 · 상태를 다시 확인한 뒤 `ok` 한 줄): 결사 `GuildCreate`(그로시 · 결사 행 · 결사장 행) · `GuildMemberSet` · `GuildMemberRemove` · `GuildTransfer` · `GuildSetInfo` ·
`GuildDisband` · `GuildMemberSeen`, 경매 `AuctionRegister`(슬롯 · 그로시 · 목록 행) · `AuctionBuy`(판매 중 확인 · 판매됨 · 구매자 그로시 · 정산 행 둘 · 시세) · `AuctionClose`(+ 지시서 이름
`AuctionCancel` · `AuctionExpire`) · `SettlementClaim`(받음 표시 · 그로시 · 슬롯), 불러오기 `LoadGuilds` · `LoadGuildMembers` · `LoadAuctionListings` · `LoadAuctionSettlements` · `LoadAuctionPrices` ·
`AuctionMaxIds`, 장애 시험 `AuctionAudit`. ODBC 쪽은 결사 이름 · 공지를 UTF-8 ↔ UTF-16(NVARCHAR)으로 바꿔 넘긴다.

### 4.3 클라

| 조각 | 파일 |
|---|---|
| 결사 창(Alt+U · 메뉴 막대): 정보(공지 · 할 수 있는 것 · 탈퇴) / 구성원(8명씩 · 머리글 눌러 정렬 · 줄을 누르면 직위 ▲▼ · 결사장 넘기기 · 추방 — 권한 있는 것만, 두 번 누르기 확인) / 관리(초대 · 공지 · 문장 · 해산) | `game/GameGuild.cpp` |
| 세우기 · 문장 창(즈데노 대화 → "결사 세우기" · "문장 바꾸기"): 이름(서버와 같은 `GuildRule::CheckName`으로 입력 중 안내) · 모양 12 · 바탕색 6 · 조건(레벨 · 그로시 · 거리) | 〃 |
| 받은 초대 카드(화면 위 가운데 · 30초 띠 · 수락 / 거절, Esc = 거절) · 세움 · 들어옴 축하(깃대에서 깃발이 펼쳐지고 문장 · 이름) · 인장 소리 | 〃 · `audio/Sfx`(Seal) |
| 머리 위: 이름 왼쪽 문장(15px) · 이름 위 `‹결사 이름›`(같은 결사는 풀빛) | `game/GameDraw.cpp` · `DrawGuildTag` |
| 문장 그리기(그림 파일 없이 도형 — 방패 바탕 · 모양 12) | `render/Emblem.{h,cpp}` |
| 경매장 창(Alt+Y · 로렌초 대화 "경매장" · "정산함"): 검색(분류 트리 · 이름 · 정렬 · 희귀도 · 각인 · 착용 레벨 · 옵션 · 6줄 · 줄에 올리면 착용 장비와 비교 툴팁) / 판매 등록(가방에서 고르기 · 끌어 놓기 · 수량 · 값 · 시세 힌트 · 기간 · 보증금 · 받을 돈) / 내 경매(내리기) / 정산함(받기 · 모두 받기) · 구매 확인 · 낙찰 도장 | `game/GameAuction.cpp` |
| 알림: 팔림(아이콘 토스트 · 동전 소리 · 채팅 한 줄) · 만료 · 메뉴 막대 점(정산함 · 받은 초대) | 〃 · `game/GameHud.cpp` · `audio/Sfx`(Gavel · CoinPour) |
| 채팅: `/초대 이름` · 이름 메뉴 "결사 초대" · `/길`(결사 채널 — 서버가 결사원에게) | `game/GameChat.cpp` |
| 여러 클라 녹화: 녹화하는 클라만 공유 시계를 올리고 나머지는 따라간다(`--sandbox-clock-follow`) | `core/Clock.cpp` · `main.cpp` |
| 시험 시나리오 | `game/GameScriptP5.cpp` · `tools/sandbox/phase5.sh` · `phase5_check.py` |
