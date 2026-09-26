-- ============================================================
-- GameServer Database Setup
-- DSN Name : DB_TermProject  (ODBC System DSN)
-- Encoding  : UTF-8 (NVARCHAR for Korean name support)
-- ============================================================

-- ----------------------------------------
-- 1. 데이터베이스 생성
-- ----------------------------------------
IF NOT EXISTS (SELECT name FROM sys.databases WHERE name = N'GameServerDB')
BEGIN
    CREATE DATABASE GameServerDB;
END
GO

USE GameServerDB;
GO

-- ----------------------------------------
-- 2. Players 테이블
--    playerId : 자동 증가 정수 PK (내부 조인/FK 전용)
--    name     : 플레이어 이름 (로그인 키, UNIQUE)
--    password : 비밀번호 (평문 저장 - 포트폴리오용)
--    x, y     : 마지막 저장 위치 (0 ~ 1999)
--    level    : 플레이어 레벨 (1~5)
--    exp      : 현재 레벨 경험치
--
--    playerId를 PK로 쓰는 이유: Inventory처럼 한 플레이어당 여러 행이 붙는
--    테이블의 FK로 최대 40바이트짜리 NVARCHAR(20) name을 그대로 쓰면 인덱스
--    비교·조인 비용이 4바이트 INT보다 훨씬 크다. name은 로그인 조회용으로
--    UNIQUE 제약만 유지한다.
-- ----------------------------------------
IF NOT EXISTS (
    SELECT * FROM sys.tables WHERE name = N'Players'
)
BEGIN
    CREATE TABLE Players (
        playerId INT           IDENTITY(1,1) NOT NULL,
        name     NVARCHAR(20)  NOT NULL,
        password NVARCHAR(20)  NOT NULL,
        x        INT           NOT NULL DEFAULT 0,
        y        INT           NOT NULL DEFAULT 0,
        level    TINYINT       NOT NULL DEFAULT 1,
        exp      INT           NOT NULL DEFAULT 0,
        CONSTRAINT PK_Players PRIMARY KEY (playerId),
        CONSTRAINT UQ_Players_Name UNIQUE (name)
    );
END
ELSE
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM sys.columns
        WHERE object_id = OBJECT_ID(N'Players') AND name = N'password'
    )
    BEGIN
        ALTER TABLE Players ADD password NVARCHAR(20) NOT NULL DEFAULT '';
    END

    IF NOT EXISTS (
        SELECT 1 FROM sys.columns
        WHERE object_id = OBJECT_ID(N'Players') AND name = N'level'
    )
    BEGIN
        ALTER TABLE Players ADD level TINYINT NOT NULL DEFAULT 1;
    END

    IF NOT EXISTS (
        SELECT 1 FROM sys.columns
        WHERE object_id = OBJECT_ID(N'Players') AND name = N'exp'
    )
    BEGIN
        ALTER TABLE Players ADD exp INT NOT NULL DEFAULT 0;
    END
END
GO

-- 예전에는 name(NVARCHAR)이 PK였던 환경을 위한 1회성 마이그레이션.
-- playerId 컬럼과 UNIQUE(name)만 먼저 추가한다 — PK를 바꾸는 건 아래 별도 블록에서
-- 처리한다(이유는 그 블록의 주석 참고).
IF NOT EXISTS (
    SELECT 1 FROM sys.columns WHERE object_id = OBJECT_ID(N'Players') AND name = N'playerId'
)
BEGIN
    ALTER TABLE Players ADD playerId INT IDENTITY(1,1);
END
GO

IF NOT EXISTS (
    SELECT 1 FROM sys.key_constraints
    WHERE name = N'UQ_Players_Name' AND parent_object_id = OBJECT_ID(N'Players')
)
BEGIN
    ALTER TABLE Players ADD CONSTRAINT UQ_Players_Name UNIQUE (name);
END
GO

-- Players의 PK를 name -> playerId로 바꾸는 블록. 컬럼 존재 여부가 아니라 "PK가
-- 지금 실제로 name 컬럼에 걸려 있는가"로 가드한다 — 예전에 이 스크립트를 돌리다
-- 이 블록에서 실패해 컬럼/UNIQUE(name)만 반영되고 PK 교체는 안 된 상태로 남는
-- 경우에도(바로 아래 문단) 재실행 시 다시 시도하도록 하기 위함이다.
--
-- 왜 실패할 수 있었나: name PK(PK_Players)는 Inventory(또는 예전 이름 PlayerItems)의
-- FK_..._Players가 참조하고 있다. UNIQUE(name)을 별도로 추가해봐야 SQL Server는
-- 기존 FK를 그 새 제약으로 자동으로 다시 묶어주지 않으므로, FK가 여전히 옛 PK를
-- 붙잡고 있어 DROP CONSTRAINT PK_Players가 그대로 실패한다("메시지 3725"). 그래서
-- PK를 바꾸기 전에 그 FK부터 먼저 지워야 한다 — 아래에서 그 순서를 지킨다.
IF EXISTS (
    SELECT 1
    FROM sys.key_constraints kc
    JOIN sys.index_columns ic ON kc.parent_object_id = ic.object_id AND kc.unique_index_id = ic.index_id
    JOIN sys.columns c ON ic.object_id = c.object_id AND ic.column_id = c.column_id
    WHERE kc.type = 'PK' AND kc.parent_object_id = OBJECT_ID(N'Players') AND c.name = N'name'
)
BEGIN
    -- 이름이 아직 PlayerItems인 환경, 이미 Inventory로 바뀐 환경 둘 다 대비한다.
    IF OBJECT_ID(N'FK_PlayerItems_Players', N'F') IS NOT NULL
        ALTER TABLE PlayerItems DROP CONSTRAINT FK_PlayerItems_Players;
    IF OBJECT_ID(N'FK_Inventory_Players', N'F') IS NOT NULL
        ALTER TABLE Inventory DROP CONSTRAINT FK_Inventory_Players;

    ALTER TABLE Players DROP CONSTRAINT PK_Players;

    -- 방금 지운/추가한 제약을 같은 배치(GO로 안 끊긴 구간) 안의 뒤쪽 문장이
    -- 참조하면 "Invalid column name" 류 바인딩 오류가 날 수 있다(SQL Server는 배치
    -- 전체를 먼저 컴파일한다). EXEC(N'...')로 감싸서 실행 시점에 별도로
    -- 컴파일되게 한다.
    EXEC(N'ALTER TABLE Players ADD CONSTRAINT PK_Players PRIMARY KEY (playerId)');
END
GO

-- ----------------------------------------
-- 3. Inventory 테이블
--    playerId  : 플레이어 (FK -> Players.playerId)
--    slotIndex : 인벤토리 슬롯 번호 (0 ~ MAX_INVENTORY_SLOTS-1)
--    itemId    : ItemTable(서버 C++ 코드의 정적 데이터)의 아이템 id
--    count     : 수량
--    equipped  : 장착 여부 (장비 아이템에만 의미 있음)
--
--    (playerId, slotIndex)를 PK로 잡아 슬롯 하나 = 행 하나로 관리한다.
--
--    예전 이름(PlayerItems)으로 이미 만든 환경을 위한 1회성 이름 변경 —
--    Inventory가 아직 없고 PlayerItems만 있으면 테이블/제약조건 이름만 바꾼다.
--    (데이터를 지우지 않는다. 새로 설치하는 환경은 이 블록이 그냥 스킵된다.)
-- ----------------------------------------
IF OBJECT_ID(N'PlayerItems', N'U') IS NOT NULL AND OBJECT_ID(N'Inventory', N'U') IS NULL
BEGIN
    EXEC sp_rename 'PlayerItems', 'Inventory';
    IF OBJECT_ID(N'PK_PlayerItems', N'PK') IS NOT NULL
        EXEC sp_rename 'PK_PlayerItems', 'PK_Inventory', 'OBJECT';
    IF OBJECT_ID(N'FK_PlayerItems_Players', N'F') IS NOT NULL
        EXEC sp_rename 'FK_PlayerItems_Players', 'FK_Inventory_Players', 'OBJECT';
END
GO

IF NOT EXISTS (
    SELECT * FROM sys.tables WHERE name = N'Inventory'
)
BEGIN
    CREATE TABLE Inventory (
        playerId  INT          NOT NULL,
        slotIndex SMALLINT     NOT NULL,
        itemId    SMALLINT     NOT NULL,
        count     SMALLINT     NOT NULL,
        equipped  BIT          NOT NULL DEFAULT 0,
        CONSTRAINT PK_Inventory PRIMARY KEY (playerId, slotIndex),
        CONSTRAINT FK_Inventory_Players FOREIGN KEY (playerId) REFERENCES Players(playerId)
    );
END
GO

-- 예전(name NVARCHAR 기반) Inventory를 playerId 기반으로 옮기는 1회성 마이그레이션.
-- (파일 순서상 위 2번 섹션에서 Players.playerId가 이미 준비되어 있다.)
IF EXISTS (
    SELECT 1 FROM sys.columns WHERE object_id = OBJECT_ID(N'Inventory') AND name = N'name'
)
BEGIN
    ALTER TABLE Inventory ADD playerId INT NULL;

    -- 같은 배치 안에서 방금 추가한 playerId를 참조하는 문장들은 전부 EXEC(N'...')로
    -- 감싼다 — 위 Players 마이그레이션과 같은 이유(배치 전체 선컴파일로 인한
    -- "Invalid column name" 오류 회피).
    EXEC(N'
        UPDATE inv
        SET inv.playerId = p.playerId
        FROM Inventory inv
        INNER JOIN Players p ON p.name = inv.name;
    ');

    IF OBJECT_ID(N'FK_Inventory_Players', N'F') IS NOT NULL
        ALTER TABLE Inventory DROP CONSTRAINT FK_Inventory_Players;
    IF OBJECT_ID(N'PK_Inventory', N'PK') IS NOT NULL
        ALTER TABLE Inventory DROP CONSTRAINT PK_Inventory;

    ALTER TABLE Inventory DROP COLUMN name;

    EXEC(N'ALTER TABLE Inventory ALTER COLUMN playerId INT NOT NULL');
    EXEC(N'ALTER TABLE Inventory ADD CONSTRAINT PK_Inventory PRIMARY KEY (playerId, slotIndex)');
    EXEC(N'ALTER TABLE Inventory ADD CONSTRAINT FK_Inventory_Players FOREIGN KEY (playerId) REFERENCES Players(playerId)');
END
GO

-- ----------------------------------------
-- 3-1. 리니지풍 컨텐츠 확장 컬럼 (1회성 마이그레이션 — 기존 데이터 보존)
--    Players.adena     : 그로시(지갑, 컬럼 이름은 예전 그대로 adena). 인벤토리 슬롯이 아니라 플레이어 단위 값이다.
--    Players.alignment : 성향(+ 명예 / - 악명). SMALLINT(-32768 ~ 32767)
--    Players.pkCount   : PK 횟수
--    Inventory.enchant : 무기 강화 수치(+0 ~ +15). 장비 슬롯에만 의미가 있다.
--
--    그로시/성향은 서버가 playerId 기준 DB 샤드로 저장한다(인벤토리와 같은 샤드).
--    그래서 한 플레이어의 "그로시 변경"과 "슬롯 변경"이 넣은 순서 그대로 커밋된다.
-- ----------------------------------------
IF NOT EXISTS (SELECT 1 FROM sys.columns WHERE object_id = OBJECT_ID(N'Players') AND name = N'adena')
    ALTER TABLE Players ADD adena INT NOT NULL CONSTRAINT DF_Players_adena DEFAULT 0;
IF NOT EXISTS (SELECT 1 FROM sys.columns WHERE object_id = OBJECT_ID(N'Players') AND name = N'alignment')
    ALTER TABLE Players ADD alignment SMALLINT NOT NULL CONSTRAINT DF_Players_alignment DEFAULT 0;
IF NOT EXISTS (SELECT 1 FROM sys.columns WHERE object_id = OBJECT_ID(N'Players') AND name = N'pkCount')
    ALTER TABLE Players ADD pkCount SMALLINT NOT NULL CONSTRAINT DF_Players_pkCount DEFAULT 0;
IF NOT EXISTS (SELECT 1 FROM sys.columns WHERE object_id = OBJECT_ID(N'Inventory') AND name = N'enchant')
    ALTER TABLE Inventory ADD enchant TINYINT NOT NULL CONSTRAINT DF_Inventory_enchant DEFAULT 0;
-- V2: 장비 인스턴스 값(희귀도 · 옵션 4줄 · 특수 효과 — 서버 Data/ItemRules.h ItemInstance).
-- 0 = 굴린 적 없는 예전 아이템/소모품(표의 기본 희귀도로 보인다). 상위 2비트는 항상 0이라 음수가 되지 않는다.
IF NOT EXISTS (SELECT 1 FROM sys.columns WHERE object_id = OBJECT_ID(N'Inventory') AND name = N'extra')
    ALTER TABLE Inventory ADD extra BIGINT NOT NULL CONSTRAINT DF_Inventory_extra DEFAULT 0;
GO

-- 예전 이름으로 만들어졌던 프로시저가 남아있으면 정리한다(위에서 새 이름으로
-- 다시 만들기 때문에, 지우지 않으면 예전 이름의 프로시저가 고아로 남는다).
IF OBJECT_ID(N'GetPlayerItems', N'P')    IS NOT NULL DROP PROCEDURE GetPlayerItems;
IF OBJECT_ID(N'SavePlayerItem', N'P')    IS NOT NULL DROP PROCEDURE SavePlayerItem;
IF OBJECT_ID(N'DeletePlayerItem', N'P')  IS NOT NULL DROP PROCEDURE DeletePlayerItem;
GO

-- ============================================================
-- 4. 저장 프로시저
-- ============================================================

-- ----------------------------------------
-- isPlayerRegistered
--   호출: EXEC isPlayerRegistered ?
--   파라미터: @name (플레이어 이름)
--   반환: BIT (1=등록됨, 0=미등록)
--
--   DBConnection::IsPlayerRegistered() 에서 호출
--   BindCol(1, SQL_BIT, ...) 로 첫 번째 컬럼을 BIT로 읽음
-- ----------------------------------------
IF OBJECT_ID(N'isPlayerRegistered', N'P') IS NOT NULL
    DROP PROCEDURE isPlayerRegistered;
GO

CREATE PROCEDURE isPlayerRegistered
    @name NVARCHAR(20)
AS
BEGIN
    SET NOCOUNT ON;

    IF EXISTS (SELECT 1 FROM Players WHERE name = @name)
        SELECT CAST(1 AS BIT) AS is_registered;
    ELSE
        SELECT CAST(0 AS BIT) AS is_registered;
END
GO

-- ----------------------------------------
-- AddNewPlayer
--   호출: EXEC AddNewPlayer ?, ?, ?, ?, ?, ?
--   파라미터: @name, @password, @x, @y, @level, @exp
--   반환: playerId (SELECT 1행) — 새로 만들었으면 새 값, 이미 있던 이름이면 기존 값.
--         서버는 이 값을 User에 캐싱해서 이후 Inventory 저장 시 name 재조회 없이 쓴다.
--
--   DBConnection::AddUserInfoInDataBase() 에서 호출
--   신규 플레이어 최초 등록 시 사용
-- ----------------------------------------
IF OBJECT_ID(N'AddNewPlayer', N'P') IS NOT NULL
    DROP PROCEDURE AddNewPlayer;
GO

CREATE PROCEDURE AddNewPlayer
    @name     NVARCHAR(20),
    @password NVARCHAR(20),
    @x        INT,
    @y        INT,
    @level    TINYINT = 1,
    @exp      INT     = 0
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    BEGIN TRAN;

    -- WITH (UPDLOCK, HOLDLOCK): 이 이름에 대한 키 범위를 트랜잭션이 끝날 때까지
    -- 잠근다. 이게 없으면 IF NOT EXISTS ... INSERT는 "확인 후 실행" 두 단계라,
    -- 정확히 같은 이름으로 동시에 두 번 호출될 경우 둘 다 "없음"으로 보고 둘 다
    -- INSERT를 시도해 하나는 PK 위반으로 실패하는 경쟁 상태(race)가 생길 수 있다.
    -- (예전 재접속 경쟁 조건 방어 주석은 이 레이스까지는 막지 못했다.)
    IF NOT EXISTS (SELECT 1 FROM Players WITH (UPDLOCK, HOLDLOCK) WHERE name = @name)
    BEGIN
        INSERT INTO Players (name, password, x, y, level, exp)
        VALUES (@name, @password, @x, @y, @level, @exp);
    END

    COMMIT TRAN;

    SELECT playerId FROM Players WHERE name = @name;
END
GO

-- ----------------------------------------
-- VerifyPlayerPassword
--   호출: EXEC VerifyPlayerPassword ?, ?
--   파라미터: @name, @password
--   반환: BIT (1=일치, 0=불일치)
--
--   DBConnection::VerifyPlayerPassword() 에서 호출
--   로그인 시 비밀번호 검증
-- ----------------------------------------
IF OBJECT_ID(N'VerifyPlayerPassword', N'P') IS NOT NULL
    DROP PROCEDURE VerifyPlayerPassword;
GO

CREATE PROCEDURE VerifyPlayerPassword
    @name     NVARCHAR(20),
    @password NVARCHAR(20)
AS
BEGIN
    SET NOCOUNT ON;

    IF EXISTS (SELECT 1 FROM Players WHERE name = @name AND password = @password)
        SELECT CAST(1 AS BIT) AS matched;
    ELSE
        SELECT CAST(0 AS BIT) AS matched;
END
GO

-- ----------------------------------------
-- ExtractPlayerInfo
--   호출: EXEC ExtractPlayerInfo ?
--   파라미터: @name
--   반환: playerId, x, y, level, exp, adena, alignment, pkCount
--         (BindCol 순서: 1=playerId, 2=x, 3=y, 4=level, 5=exp, 6=adena, 7=alignment, 8=pkCount)
--
--   DBConnection::ExtractUserInfo() 에서 호출
--   기존 플레이어 로그인 시 저장 데이터 복원. playerId는 서버가 User에 캐싱해서
--   이후 Inventory 저장 시 name 재조회 없이 쓴다.
-- ----------------------------------------
IF OBJECT_ID(N'ExtractPlayerInfo', N'P') IS NOT NULL
    DROP PROCEDURE ExtractPlayerInfo;
GO

CREATE PROCEDURE ExtractPlayerInfo
    @name NVARCHAR(20)
AS
BEGIN
    SET NOCOUNT ON;

    SELECT playerId, x, y, level, exp, adena, alignment, pkCount
    FROM Players
    WHERE name = @name;
END
GO

-- ----------------------------------------
-- SavePlayerInfo
--   호출: EXEC SavePlayerInfo ?, ?, ?, ?, ?
--   파라미터: @name, @x, @y, @level, @exp
--   반환: 없음
--
--   DBConnection::SaveUserInfo() 에서 호출
--   플레이어 이동/로그아웃 시 위치·경험치 저장
-- ----------------------------------------
IF OBJECT_ID(N'SavePlayerInfo', N'P') IS NOT NULL
    DROP PROCEDURE SavePlayerInfo;
GO

CREATE PROCEDURE SavePlayerInfo
    @name  NVARCHAR(20),
    @x     INT,
    @y     INT,
    @level TINYINT,
    @exp   INT
AS
BEGIN
    SET NOCOUNT ON;

    UPDATE Players
    SET x     = CASE WHEN @x BETWEEN 0 AND 1999 THEN @x ELSE x END,
        y     = CASE WHEN @y BETWEEN 0 AND 1999 THEN @y ELSE y END,
        level = @level,
        exp   = @exp
    WHERE name = @name;
END
GO

-- ----------------------------------------
-- GetInventory
--   호출: EXEC GetInventory ?
--   파라미터: @playerId (서버가 로그인 시 확보해 캐싱해둔 정수 PK — name 조회 없음)
--   반환: slotIndex, itemId, count, equipped, enchant, extra (행마다 슬롯 하나)
--
--   DBConnection::ExtractInventory() 에서 호출
--   로그인 시 인벤토리 전체 로드
-- ----------------------------------------
IF OBJECT_ID(N'GetInventory', N'P') IS NOT NULL
    DROP PROCEDURE GetInventory;
GO

CREATE PROCEDURE GetInventory
    @playerId INT
AS
BEGIN
    SET NOCOUNT ON;

    SELECT slotIndex, itemId, count, equipped, enchant, extra
    FROM Inventory
    WHERE playerId = @playerId;
END
GO

-- ----------------------------------------
-- SaveInventorySlot
--   호출: EXEC SaveInventorySlot ?, ?, ?, ?, ?, ?, ?
--   파라미터: @playerId, @slotIndex, @itemId, @count, @equipped, @enchant, @extra
--   반환: 없음
--
--   DBConnection::SaveInventorySlot() 에서 호출
--   슬롯을 새로 만들거나(획득) 기존 슬롯을 갱신(수량 변경/장착/탈착/교체)할 때 사용.
--   Upsert: 해당 (playerId, slotIndex) 행이 있으면 갱신, 없으면 삽입.
--   (이전에는 @name -> playerId 변환을 위해 Players까지 조회/잠갔는데, 서버가
--   playerId를 이미 들고 있으므로 이 프로시저는 이제 Inventory 테이블만 건드린다.)
-- ----------------------------------------
IF OBJECT_ID(N'SaveInventorySlot', N'P') IS NOT NULL
    DROP PROCEDURE SaveInventorySlot;
GO

CREATE PROCEDURE SaveInventorySlot
    @playerId  INT,
    @slotIndex SMALLINT,
    @itemId    SMALLINT,
    @count     SMALLINT,
    @equipped  BIT,
    @enchant   TINYINT = 0,
    @extra     BIGINT  = 0
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    BEGIN TRAN;

    -- UPDATE를 먼저 시도하고, 실제로 바뀐 행이 없으면(@@ROWCOUNT=0) 그때 INSERT한다.
    -- WITH (UPDLOCK, HOLDLOCK): 이미 있는 행은 UPDLOCK으로, 아직 없는 (playerId,
    -- slotIndex) 키 범위는 HOLDLOCK(SERIALIZABLE)으로 트랜잭션이 끝날 때까지
    -- 잠근다 — 그래야 같은 슬롯에 대해 정확히 같은 순간에 두 저장 요청이 들어와도
    -- 하나가 끝난 뒤에야 다음 게 실행되어, 이전의 "IF EXISTS ... ELSE INSERT"
    -- 방식이 갖고 있던 이중 INSERT/유실 갱신(lost update) 경쟁 상태가 사라진다.
    UPDATE Inventory WITH (UPDLOCK, HOLDLOCK)
    SET itemId = @itemId, count = @count, equipped = @equipped, enchant = @enchant, extra = @extra
    WHERE playerId = @playerId AND slotIndex = @slotIndex;

    IF @@ROWCOUNT = 0
    BEGIN
        INSERT INTO Inventory (playerId, slotIndex, itemId, count, equipped, enchant, extra)
        VALUES (@playerId, @slotIndex, @itemId, @count, @equipped, @enchant, @extra);
    END

    COMMIT TRAN;
END
GO

-- ----------------------------------------
-- DeleteInventorySlot
--   호출: EXEC DeleteInventorySlot ?, ?
--   파라미터: @playerId, @slotIndex
--   반환: 없음
--
--   DBConnection::DeleteInventorySlot() 에서 호출
--   슬롯이 완전히 비워졌을 때(수량이 0이 됨) 해당 행을 제거.
-- ----------------------------------------
IF OBJECT_ID(N'DeleteInventorySlot', N'P') IS NOT NULL
    DROP PROCEDURE DeleteInventorySlot;
GO

CREATE PROCEDURE DeleteInventorySlot
    @playerId  INT,
    @slotIndex SMALLINT
AS
BEGIN
    SET NOCOUNT ON;

    DELETE FROM Inventory
    WHERE playerId = @playerId AND slotIndex = @slotIndex;
END
GO

-- ----------------------------------------
-- SaveTwoInventorySlots
--   호출: EXEC SaveTwoInventorySlots ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?
--   파라미터: @playerId,
--             @slotIndexA, @hasA, @itemIdA, @countA, @equippedA,
--             @slotIndexB, @hasB, @itemIdB, @countB, @equippedB,
--             @enchantA, @enchantB   (강화 수치 — 기존 호출 순서를 깨지 않도록 끝에 붙였다)
--             @extraA, @extraB       (V2 희귀도·옵션 — 같은 이유로 그 뒤에 붙였다)
--   반환: 없음
--
--   DBConnection::SaveTwoInventorySlots() 에서 호출
--   두 슬롯이 "논리적으로 하나의 동작"인 경우(장착 시 이전 장비 자동 탈착,
--   슬롯 교체) 전용. @hasA/@hasB=1이면 그 슬롯을 upsert, 0이면 삭제한다.
--   SaveInventorySlot/DeleteInventorySlot을 두 번 따로 부르면 그 사이에 서버가
--   죽었을 때 한쪽만 반영된 상태가 DB에 영구히 남을 수 있다 — 그래서 반드시
--   하나의 트랜잭션으로 묶는다.
--
--   [락 순서 고정] 이 프로시저는 한 트랜잭션 안에서 행 두 개를 잠근다. 호출자가
--   넘긴 A/B 순서 그대로 잠그면, 같은 두 슬롯을 반대 방향으로 다루는 두 호출
--   (예: 슬롯 5<->8 교체와 8<->5 교체)이 겹칠 때 서로가 상대의 행을 기다리는
--   전형적인 락 순서 데드락이 생긴다. 그래서 맨 앞에서 항상 "슬롯 번호가 작은
--   쪽"이 A가 되도록 정렬한 뒤 진행한다 — 어떤 호출이든 잠그는 순서가 언제나
--   같아지므로 서로 엇갈려 물릴 수가 없다. (두 블록은 순서와 무관하게 각자
--   자기 슬롯만 건드리므로, 순서를 바꿔도 결과는 완전히 동일하다.)
-- ----------------------------------------
IF OBJECT_ID(N'SaveTwoInventorySlots', N'P') IS NOT NULL
    DROP PROCEDURE SaveTwoInventorySlots;
GO

CREATE PROCEDURE SaveTwoInventorySlots
    @playerId   INT,
    @slotIndexA SMALLINT,
    @hasA       BIT,
    @itemIdA    SMALLINT,
    @countA     SMALLINT,
    @equippedA  BIT,
    @slotIndexB SMALLINT,
    @hasB       BIT,
    @itemIdB    SMALLINT,
    @countB     SMALLINT,
    @equippedB  BIT,
    @enchantA   TINYINT = 0,
    @enchantB   TINYINT = 0,
    @extraA     BIGINT  = 0,
    @extraB     BIGINT  = 0
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    -- 항상 슬롯 번호가 작은 쪽을 A로 만들어, 어떤 호출이든 잠그는 순서를 동일하게
    -- 맞춘다(위 주석의 [락 순서 고정] 참고). 두 슬롯이 통째로 맞바뀌므로
    -- has/itemId/count/equipped도 짝을 맞춰 함께 바꿔야 한다.
    IF @slotIndexA > @slotIndexB
    BEGIN
        DECLARE @tmpSlot SMALLINT, @tmpHas BIT, @tmpItemId SMALLINT, @tmpCount SMALLINT, @tmpEquipped BIT, @tmpEnchant TINYINT, @tmpExtra BIGINT;

        SELECT @tmpSlot     = @slotIndexA,
               @tmpHas      = @hasA,
               @tmpItemId   = @itemIdA,
               @tmpCount    = @countA,
               @tmpEquipped = @equippedA,
               @tmpEnchant  = @enchantA,
               @tmpExtra    = @extraA;

        SELECT @slotIndexA = @slotIndexB,
               @hasA       = @hasB,
               @itemIdA    = @itemIdB,
               @countA     = @countB,
               @equippedA  = @equippedB,
               @enchantA   = @enchantB,
               @extraA     = @extraB;

        SELECT @slotIndexB = @tmpSlot,
               @hasB       = @tmpHas,
               @itemIdB    = @tmpItemId,
               @countB     = @tmpCount,
               @equippedB  = @tmpEquipped,
               @enchantB   = @tmpEnchant,
               @extraB     = @tmpExtra;
    END

    BEGIN TRAN;

    IF @hasA = 1
    BEGIN
        UPDATE Inventory WITH (UPDLOCK, HOLDLOCK)
        SET itemId = @itemIdA, count = @countA, equipped = @equippedA, enchant = @enchantA, extra = @extraA
        WHERE playerId = @playerId AND slotIndex = @slotIndexA;

        IF @@ROWCOUNT = 0
            INSERT INTO Inventory (playerId, slotIndex, itemId, count, equipped, enchant, extra)
            VALUES (@playerId, @slotIndexA, @itemIdA, @countA, @equippedA, @enchantA, @extraA);
    END
    ELSE
    BEGIN
        DELETE FROM Inventory WHERE playerId = @playerId AND slotIndex = @slotIndexA;
    END

    IF @hasB = 1
    BEGIN
        UPDATE Inventory WITH (UPDLOCK, HOLDLOCK)
        SET itemId = @itemIdB, count = @countB, equipped = @equippedB, enchant = @enchantB, extra = @extraB
        WHERE playerId = @playerId AND slotIndex = @slotIndexB;

        IF @@ROWCOUNT = 0
            INSERT INTO Inventory (playerId, slotIndex, itemId, count, equipped, enchant, extra)
            VALUES (@playerId, @slotIndexB, @itemIdB, @countB, @equippedB, @enchantB, @extraB);
    END
    ELSE
    BEGIN
        DELETE FROM Inventory WHERE playerId = @playerId AND slotIndex = @slotIndexB;
    END

    COMMIT TRAN;
END
GO

-- ----------------------------------------
-- SavePlayerAdena
--   호출: EXEC SavePlayerAdena ?, ?
--   파라미터: @playerId, @adena (절대값 — 서버 메모리의 최신 지갑)
--
--   DBConnection::SavePlayerAdena() 에서 호출. 그로시 줍기/옛길 이동 요금처럼
--   슬롯 변화 없이 지갑만 바뀐 경우. 델타가 아니라 절대값이라 같은 샤드 안의
--   순서만 지켜지면 마지막 커밋이 곧 최신 상태다(메모리가 권위, DB는 저널).
-- ----------------------------------------
IF OBJECT_ID(N'SavePlayerAdena', N'P') IS NOT NULL
    DROP PROCEDURE SavePlayerAdena;
GO

CREATE PROCEDURE SavePlayerAdena
    @playerId INT,
    @adena    INT
AS
BEGIN
    SET NOCOUNT ON;

    UPDATE Players SET adena = CASE WHEN @adena >= 0 THEN @adena ELSE adena END
    WHERE playerId = @playerId;
END
GO

-- ----------------------------------------
-- SavePlayerAlignment
--   호출: EXEC SavePlayerAlignment ?, ?, ?
--   파라미터: @playerId, @alignment, @pkCount
--
--   DBConnection::SavePlayerAlignment() 에서 호출. 사냥으로 조금씩 오르는 값이라
--   서버가 일정 폭 이상 변했을 때 / PK 직후 / 로그아웃 때만 저장한다.
-- ----------------------------------------
IF OBJECT_ID(N'SavePlayerAlignment', N'P') IS NOT NULL
    DROP PROCEDURE SavePlayerAlignment;
GO

CREATE PROCEDURE SavePlayerAlignment
    @playerId  INT,
    @alignment SMALLINT,
    @pkCount   SMALLINT
AS
BEGIN
    SET NOCOUNT ON;

    UPDATE Players SET alignment = @alignment, pkCount = @pkCount
    WHERE playerId = @playerId;
END
GO

-- ----------------------------------------
-- ShopTransaction
--   호출: EXEC ShopTransaction ?, ?, ?, ?, ?, ?, ?, ?
--   파라미터: @playerId, @adena, @slotIndex, @hasItem, @itemId, @count, @equipped, @enchant
--
--   DBConnection::ShopTransaction() 에서 호출. 상점 구매/판매는 "그로시"와 "슬롯"이
--   반드시 함께 바뀌어야 하는 동작이다 — 따로 저장하다 그 사이에 서버가 죽으면
--   "돈만 나가고 물건이 없음" 또는 "물건은 생겼는데 돈이 그대로(복사)"가 DB에 남는다.
--   그래서 하나의 트랜잭션으로 묶는다. (서버는 한 슬롯에 다 들어가는 거래만 받는다 —
--   Inventory::TryBuy 참고 — 그래서 슬롯 하나 + 그로시로 충분하다.)
-- ----------------------------------------
IF OBJECT_ID(N'ShopTransaction', N'P') IS NOT NULL
    DROP PROCEDURE ShopTransaction;
GO

CREATE PROCEDURE ShopTransaction
    @playerId  INT,
    @adena     INT,
    @slotIndex SMALLINT,
    @hasItem   BIT,
    @itemId    SMALLINT,
    @count     SMALLINT,
    @equipped  BIT,
    @enchant   TINYINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    BEGIN TRAN;

    UPDATE Players SET adena = @adena WHERE playerId = @playerId;

    IF @hasItem = 1
    BEGIN
        -- 상점 거래 슬롯은 굴린 적 없는 물건(소모품·새 장비)이거나 남은 스택이라 extra는 항상 0이다.
        UPDATE Inventory WITH (UPDLOCK, HOLDLOCK)
        SET itemId = @itemId, count = @count, equipped = @equipped, enchant = @enchant, extra = 0
        WHERE playerId = @playerId AND slotIndex = @slotIndex;

        IF @@ROWCOUNT = 0
            INSERT INTO Inventory (playerId, slotIndex, itemId, count, equipped, enchant, extra)
            VALUES (@playerId, @slotIndex, @itemId, @count, @equipped, @enchant, 0);
    END
    ELSE
    BEGIN
        DELETE FROM Inventory WHERE playerId = @playerId AND slotIndex = @slotIndex;
    END

    COMMIT TRAN;
END
GO

-- ============================================================
-- 4-1. V2 캐릭터 진행 데이터 (직업·능력치 포인트·퀘스트 등)
--   기능이 늘 때마다 Players에 컬럼을 늘리는 대신, 서버가 버전 있는 바이너리 한
--   덩어리로 읽고 쓴다(User::SerializeProgress). 없으면 기본값(서약기사)으로 시작.
-- ============================================================
IF OBJECT_ID(N'PlayerProgress', N'U') IS NULL
BEGIN
    CREATE TABLE PlayerProgress (
        playerId  INT             NOT NULL PRIMARY KEY,
        data      VARBINARY(4000) NOT NULL,
        updatedAt DATETIME2       NOT NULL CONSTRAINT DF_PlayerProgress_updatedAt DEFAULT SYSUTCDATETIME(),
        CONSTRAINT FK_PlayerProgress_Players FOREIGN KEY (playerId) REFERENCES Players(playerId) ON DELETE CASCADE
    );
END
GO

IF OBJECT_ID(N'LoadPlayerProgress', N'P') IS NOT NULL
    DROP PROCEDURE LoadPlayerProgress;
GO

CREATE PROCEDURE LoadPlayerProgress
    @playerId INT
AS
BEGIN
    SET NOCOUNT ON;
    SELECT data FROM PlayerProgress WHERE playerId = @playerId;
END
GO

IF OBJECT_ID(N'SavePlayerProgress', N'P') IS NOT NULL
    DROP PROCEDURE SavePlayerProgress;
GO

CREATE PROCEDURE SavePlayerProgress
    @playerId INT,
    @data     VARBINARY(4000)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    BEGIN TRAN;
    UPDATE PlayerProgress WITH (UPDLOCK, HOLDLOCK)
    SET data = @data, updatedAt = SYSUTCDATETIME()
    WHERE playerId = @playerId;
    IF @@ROWCOUNT = 0
        INSERT INTO PlayerProgress (playerId, data) VALUES (@playerId, @data);
    COMMIT TRAN;
END
GO

-- ============================================================
-- 4-2. Phase 2-D 기술 성장 (Data/SkillGrowth.h)
--   기술 레벨 1~10 · 갈래(레벨 4 · 7에서 둘 중 하나). 레벨 1 · 갈래 없음은 행이 없다(기본값).
--   남은 기술 점수는 저장하지 않는다 — 캐릭터 레벨과 기술 레벨들에서 서버가 다시 계산한다.
--   새 테이블만 만든다(IF 없으면) — 기존 테이블 · 데이터는 건드리지 않는다.
-- ============================================================
IF OBJECT_ID(N'PlayerSkills', N'U') IS NULL
BEGIN
    CREATE TABLE PlayerSkills (
        playerId  INT       NOT NULL,
        skillId   SMALLINT  NOT NULL,
        level     TINYINT   NOT NULL CONSTRAINT DF_PlayerSkills_level DEFAULT 1,
        pick1     TINYINT   NOT NULL CONSTRAINT DF_PlayerSkills_pick1 DEFAULT 0,
        pick2     TINYINT   NOT NULL CONSTRAINT DF_PlayerSkills_pick2 DEFAULT 0,
        updatedAt DATETIME2 NOT NULL CONSTRAINT DF_PlayerSkills_updatedAt DEFAULT SYSUTCDATETIME(),
        CONSTRAINT PK_PlayerSkills PRIMARY KEY (playerId, skillId),
        CONSTRAINT CK_PlayerSkills_level CHECK (level BETWEEN 1 AND 10),
        CONSTRAINT CK_PlayerSkills_pick CHECK (pick1 BETWEEN 0 AND 2 AND pick2 BETWEEN 0 AND 2),
        CONSTRAINT FK_PlayerSkills_Players FOREIGN KEY (playerId) REFERENCES Players(playerId) ON DELETE CASCADE
    );
END
GO

IF OBJECT_ID(N'LoadPlayerSkills', N'P') IS NOT NULL
    DROP PROCEDURE LoadPlayerSkills;
GO

CREATE PROCEDURE LoadPlayerSkills
    @playerId INT
AS
BEGIN
    SET NOCOUNT ON;
    SELECT skillId, level, pick1, pick2 FROM PlayerSkills WHERE playerId = @playerId;
END
GO

IF OBJECT_ID(N'SavePlayerSkill', N'P') IS NOT NULL
    DROP PROCEDURE SavePlayerSkill;
GO

-- 기술 하나의 바뀐 뒤 상태(절대값) — 같은 값이 두 번 와도 결과가 같다. 기본값(레벨 1 · 갈래 없음)이면 행을 지운다.
CREATE PROCEDURE SavePlayerSkill
    @playerId INT,
    @skillId  SMALLINT,
    @level    TINYINT,
    @pick1    TINYINT,
    @pick2    TINYINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    BEGIN TRAN;
    IF @level <= 1 AND @pick1 = 0 AND @pick2 = 0
    BEGIN
        DELETE FROM PlayerSkills WHERE playerId = @playerId AND skillId = @skillId;
    END
    ELSE
    BEGIN
        UPDATE PlayerSkills WITH (UPDLOCK, HOLDLOCK)
        SET level = @level, pick1 = @pick1, pick2 = @pick2, updatedAt = SYSUTCDATETIME()
        WHERE playerId = @playerId AND skillId = @skillId;
        IF @@ROWCOUNT = 0
            INSERT INTO PlayerSkills (playerId, skillId, level, pick1, pick2)
            VALUES (@playerId, @skillId, @level, @pick1, @pick2);
    END
    COMMIT TRAN;
END
GO

IF OBJECT_ID(N'ResetPlayerSkills', N'P') IS NOT NULL
    DROP PROCEDURE ResetPlayerSkills;
GO

-- 모두 초기화(점수를 돌려받는다) — 한 문장이라 그대로 원자적이다
CREATE PROCEDURE ResetPlayerSkills
    @playerId INT
AS
BEGIN
    SET NOCOUNT ON;
    DELETE FROM PlayerSkills WHERE playerId = @playerId;
END
GO

-- ============================================================
-- 5. ODBC System DSN 설정 안내 (64비트 빌드 기준)
-- ============================================================
-- 관리자 권한 PowerShell에서 아래 명령어 실행:
--
--   Add-OdbcDsn -Name "DB_TermProject" -DriverName "ODBC Driver 17 for SQL Server" `
--               -DsnType "System" -Platform "64-bit" `
--               -SetPropertyValue @("Server=(local)", "Database=GameServerDB", "Trusted_Connection=Yes")
--
-- 또는 수동 등록:
--   실행 경로 : C:\Windows\System32\odbcad32.exe  (64비트 ODBC 관리자)
--   이름(N)   : DB_TermProject          <- DBConnection.cpp L43 에서 사용
--   드라이버  : ODBC Driver 17 for SQL Server
--   서버      : (local)
--   기본 DB   : GameServerDB
--   인증      : Windows 통합 인증
-- ============================================================
