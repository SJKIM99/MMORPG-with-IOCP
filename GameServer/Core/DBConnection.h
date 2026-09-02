#pragma once
#include <sql.h>
#include <sqlext.h>
using namespace std;

class DBConnection
{
public:
	DBConnection() = default;
	~DBConnection();

	bool			Connect();
	void			Clear();

	bool			Execute(const WCHAR* query);
	bool			Fetch();
	int				GetRowCount();
	void			Unbind();

public:
	bool			BindParam(SQLUSMALLINT paramIndex, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN len, SQLPOINTER ptr, SQLLEN* index);
	bool			BindCol(SQLUSMALLINT columnIndex, SQLSMALLINT cType, SQLULEN len, SQLPOINTER value, SQLLEN* index);
	void			HandleError(SQLRETURN ret);

public:
	bool			IsUserRegistered(const string& name);
	bool			VerifyUserPassword(const string& name, const string& password);
	// 성공 시 outPlayerId에 Players.playerId(신규 삽입이면 새로 발급된 값, 이미
	// 존재하는 이름이면 그 행의 값)를 채운다. 실패하면 false를 반환하고 건드리지 않는다.
	bool			AddUserInfoInDataBase(const string& name, const string& password, short x, short y, uint8 level, uint32 exp, int& outPlayerId);
	DB_USER_INFO	ExtractUserInfo(const string& name);
	bool			SaveUserInfo(const string& name, short x, short y, uint8 level, uint32 exp);

	// playerId는 로그인/계정생성 시 확보해 User에 캐싱해둔 정수 PK를 그대로 쓴다 —
	// name(NVARCHAR) 기반 조회/조인이 DB 쪽에서 완전히 사라진다.
	vector<DB_ITEM_INFO> ExtractInventory(int playerId);
	bool			SaveInventorySlot(int playerId, uint16_t slotIndex, uint16_t itemId, uint16_t count, bool equipped);
	bool			DeleteInventorySlot(int playerId, uint16_t slotIndex);

	// a/b 두 슬롯을 하나의 SQL 트랜잭션으로 함께 반영한다(각 슬롯은 독립적으로
	// upsert 또는 삭제될 수 있다 — DB_ITEM_SLOT_SAVE::hasItem 참고). 장착 시 이전
	// 장비 자동 탈착, 슬롯 교체처럼 "두 슬롯이 논리적으로 한 동작"인 경우 반드시
	// 이걸 쓴다 — SaveInventorySlot을 두 번 따로 부르면 그 사이에 서버가 죽었을 때
	// DB에 한쪽만 반영된 불변조건 위반 상태가 남을 수 있다.
	bool			SaveTwoInventorySlots(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b);

private:
	SQLHENV			_enviroment = SQL_NULL_HANDLE;
	SQLHDBC			_connection = SQL_NULL_HANDLE;
	SQLHSTMT		_statement = SQL_NULL_HANDLE;
	SQLRETURN		_retcode = SQL_SUCCESS;
};
