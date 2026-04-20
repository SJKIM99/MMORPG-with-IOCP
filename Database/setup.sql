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
--    name     : 플레이어 이름 (PK, 로그인 키)
--    password : 비밀번호 (평문 저장 - 포트폴리오용)
--    x, y     : 마지막 저장 위치 (0 ~ 1999)
--    level    : 플레이어 레벨 (1~5)
--    exp      : 현재 레벨 경험치
-- ----------------------------------------
IF NOT EXISTS (
    SELECT * FROM sys.tables WHERE name = N'Players'
)
BEGIN
    CREATE TABLE Players (
        name     NVARCHAR(20)  NOT NULL,
        password NVARCHAR(20)  NOT NULL,
        x        INT           NOT NULL DEFAULT 0,
        y        INT           NOT NULL DEFAULT 0,
        level    TINYINT       NOT NULL DEFAULT 1,
        exp      INT           NOT NULL DEFAULT 0,
        CONSTRAINT PK_Players PRIMARY KEY (name)
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

-- ============================================================
-- 3. 저장 프로시저
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
--   반환: 없음
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

    -- 중복 이름이면 무시 (재접속 경쟁 조건 방어)
    IF NOT EXISTS (SELECT 1 FROM Players WHERE name = @name)
    BEGIN
        INSERT INTO Players (name, password, x, y, level, exp)
        VALUES (@name, @password, @x, @y, @level, @exp);
    END
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
--   반환: x, y, level, exp  (BindCol 순서: 1=x, 2=y, 3=level, 4=exp)
--
--   DBConnection::ExtractUserInfo() 에서 호출
--   기존 플레이어 로그인 시 저장 데이터 복원
-- ----------------------------------------
IF OBJECT_ID(N'ExtractPlayerInfo', N'P') IS NOT NULL
    DROP PROCEDURE ExtractPlayerInfo;
GO

CREATE PROCEDURE ExtractPlayerInfo
    @name NVARCHAR(20)
AS
BEGIN
    SET NOCOUNT ON;

    SELECT x, y, level, exp
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

-- ============================================================
-- 4. ODBC System DSN 설정 안내 (64비트 빌드 기준)
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
