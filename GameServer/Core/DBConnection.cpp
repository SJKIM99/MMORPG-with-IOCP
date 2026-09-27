#include "pch.h"
#include "DBConnection.h"

namespace
{
	struct StatementCleanup
	{
		explicit StatementCleanup(DBConnection& connection) : _connection(connection) { }
		~StatementCleanup() { _connection.Unbind(); }

		DBConnection& _connection;
	};
}

DBConnection::~DBConnection()
{
	Clear();
}

bool DBConnection::Connect()
{
	// Allocate environment handle
	_retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &_enviroment);
	if (_retcode != SQL_SUCCESS && _retcode != SQL_SUCCESS_WITH_INFO)
		return false;

	// Set ODBC version environment attribute
	_retcode = SQLSetEnvAttr(_enviroment, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
	if (_retcode != SQL_SUCCESS && _retcode != SQL_SUCCESS_WITH_INFO)
		return false;

	// Allocate connection handle
	_retcode = SQLAllocHandle(SQL_HANDLE_DBC, _enviroment, &_connection);
	if (_retcode != SQL_SUCCESS && _retcode != SQL_SUCCESS_WITH_INFO)
		return false;

	// Set login timeout to 5 seconds
	_retcode = SQLSetConnectAttr(_connection, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
	if (_retcode != SQL_SUCCESS && _retcode != SQL_SUCCESS_WITH_INFO)
		return false;

	// Connect to the data source
	_retcode = SQLConnect(_connection, (SQLWCHAR*)L"DB_TermProject", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);
	if (_retcode != SQL_SUCCESS && _retcode != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(_retcode);
		return false;
	}

	// Allocate statement handle
	_retcode = SQLAllocHandle(SQL_HANDLE_STMT, _connection, &_statement);
	if (_retcode != SQL_SUCCESS && _retcode != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(_retcode);
		return false;
	}

	return true;
}

void DBConnection::Clear()
{
	if (_statement != SQL_NULL_HANDLE)
	{
		::SQLFreeHandle(SQL_HANDLE_STMT, _statement);
		_statement = SQL_NULL_HANDLE;
	}

	if (_connection != SQL_NULL_HANDLE)
	{
		::SQLDisconnect(_connection);
		::SQLFreeHandle(SQL_HANDLE_DBC, _connection);
		_connection = SQL_NULL_HANDLE;
	}

	if (_enviroment != SQL_NULL_HANDLE)
	{
		::SQLFreeHandle(SQL_HANDLE_ENV, _enviroment);
		_enviroment = SQL_NULL_HANDLE;
	}
}

bool DBConnection::Execute(const WCHAR* query)
{
	SQLRETURN ret = ::SQLExecDirectW(_statement, (SQLWCHAR*)query, SQL_NTS);
	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
		return true;

	HandleError(ret);
	return false;
}

bool DBConnection::Fetch()
{
	SQLRETURN ret = ::SQLFetch(_statement);

	switch (ret)
	{
	case SQL_SUCCESS:
	case SQL_SUCCESS_WITH_INFO:
		return true;
	case SQL_NO_DATA:
		return false;
	case SQL_ERROR:
		HandleError(ret);
		return false;
	default:
		return true;
	}
}

int DBConnection::GetRowCount()
{
	SQLLEN count = 0;
	SQLRETURN ret = ::SQLRowCount(_statement, OUT & count);

	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
		return static_cast<int>(count);

	return -1;
}

void DBConnection::Unbind()
{
	::SQLFreeStmt(_statement, SQL_UNBIND);
	::SQLFreeStmt(_statement, SQL_RESET_PARAMS);
	::SQLFreeStmt(_statement, SQL_CLOSE);
}

bool DBConnection::BindParam(SQLUSMALLINT paramIndex, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN len, SQLPOINTER ptr, SQLLEN* index)
{
	SQLRETURN ret = ::SQLBindParameter(_statement, paramIndex, SQL_PARAM_INPUT, cType, sqlType, len, 0, ptr, 0, index);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(ret);
		return false;
	}

	return true;
}

bool DBConnection::BindCol(SQLUSMALLINT columnIndex, SQLSMALLINT cType, SQLULEN len, SQLPOINTER value, SQLLEN* index)
{
	SQLRETURN ret = ::SQLBindCol(_statement, columnIndex, cType, value, len, index);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(ret);
		return false;
	}

	return true;
}

void DBConnection::HandleError(SQLRETURN ret)
{
	if (ret == SQL_SUCCESS)
		return;

	SQLSMALLINT handleType = SQL_HANDLE_STMT;
	SQLHANDLE handle = _statement;

	if (handle == SQL_NULL_HANDLE)
	{
		handleType = SQL_HANDLE_DBC;
		handle = _connection;
	}

	if (handle == SQL_NULL_HANDLE)
	{
		handleType = SQL_HANDLE_ENV;
		handle = _enviroment;
	}

	if (handle == SQL_NULL_HANDLE)
		return;

	SQLSMALLINT index = 1;
	SQLWCHAR sqlState[MAX_PATH] = { 0 };
	SQLINTEGER nativeErr = 0;
	SQLWCHAR errMsg[MAX_PATH] = { 0 };
	SQLSMALLINT msgLen = 0;
	SQLRETURN errorRet = 0;

	while (true)
	{
		errorRet = ::SQLGetDiagRecW(
			handleType,
			handle,
			index,
			sqlState,
			OUT & nativeErr,
			errMsg,
			_countof(errMsg),
			OUT & msgLen
		);

		if (errorRet == SQL_NO_DATA)
			break;

		if (errorRet != SQL_SUCCESS && errorRet != SQL_SUCCESS_WITH_INFO)
			break;

		// TODO : Log
		wcout.imbue(locale("kor"));
		wcout << errMsg << endl;

		index++;
	}
}

bool DBConnection::IsUserRegistered(const string& name)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC isPlayerRegistered ?";

	if (!BindParam(1, SQL_C_CHAR, SQL_WVARCHAR, name.size(), (SQLPOINTER)name.c_str(), nullptr)) return false;
	if (!Execute(query.c_str())) return false;

	SQLCHAR isRegistered{};
	SQLLEN cb_isRegistered{};

	if (!BindCol(1, SQL_BIT, sizeof(isRegistered), &isRegistered, &cb_isRegistered)) return false;
	if (!Fetch()) return false;

	return (isRegistered == 1);
}

bool DBConnection::VerifyUserPassword(const string& name, const string& password)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC VerifyPlayerPassword ?, ?";

	if (!BindParam(1, SQL_C_CHAR, SQL_WVARCHAR, name.size(),     (SQLPOINTER)name.c_str(),     nullptr)) return false;
	if (!BindParam(2, SQL_C_CHAR, SQL_WVARCHAR, password.size(), (SQLPOINTER)password.c_str(), nullptr)) return false;
	if (!Execute(query.c_str())) return false;

	SQLCHAR matched{};
	SQLLEN  cb_matched{};
	if (!BindCol(1, SQL_BIT, sizeof(matched), &matched, &cb_matched)) return false;
	if (!Fetch()) return false;

	return (matched == 1);
}

bool DBConnection::AddUserInfoInDataBase(const string& name, const string& password, short x, short y, uint8 level, uint32 exp, int& outPlayerId)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC AddNewPlayer ?, ?, ?, ?, ?, ?";

	if (!BindParam(1, SQL_C_CHAR,  SQL_VARCHAR,  name.size(),     (SQLPOINTER)name.c_str(),     nullptr)) return false;
	if (!BindParam(2, SQL_C_CHAR,  SQL_VARCHAR,  password.size(), (SQLPOINTER)password.c_str(), nullptr)) return false;
	if (!BindParam(3, SQL_C_SHORT, SQL_INTEGER,  0,               (SQLPOINTER)&x,               nullptr)) return false;
	if (!BindParam(4, SQL_C_SHORT, SQL_INTEGER,  0,               (SQLPOINTER)&y,               nullptr)) return false;
	if (!BindParam(5, SQL_C_UTINYINT, SQL_TINYINT, 0,             (SQLPOINTER)&level,           nullptr)) return false;
	if (!BindParam(6, SQL_C_ULONG, SQL_INTEGER,  0,               (SQLPOINTER)&exp,             nullptr)) return false;
	if (!Execute(query.c_str())) return false;

	SQLINTEGER playerId{};
	SQLLEN cb_playerId{};
	if (!BindCol(1, SQL_INTEGER, sizeof(playerId), &playerId, &cb_playerId)) return false;
	if (!Fetch()) return false;

	outPlayerId = static_cast<int>(playerId);
	return true;
}

DB_PLAYER_INFO DBConnection::ExtractUserInfo(const string& name)
{
	DB_PLAYER_INFO playerInfo{};
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC ExtractPlayerInfo ?";

	SQLINTEGER  player_id{};
	SQLINTEGER  player_x{}, player_y{};
	SQLCHAR     player_level{};
	SQLUINTEGER player_exp{};
	SQLINTEGER  player_adena{};
	SQLSMALLINT player_alignment{}, player_pk{};
	SQLLEN cb_id{}, cb_x{}, cb_y{}, cb_level{}, cb_exp{}, cb_adena{}, cb_alignment{}, cb_pk{};

	if (!BindParam(1, SQL_C_CHAR, SQL_WVARCHAR, name.size(), (SQLPOINTER)name.c_str(), nullptr)) return playerInfo;
	if (!Execute(query.c_str())) return playerInfo;
	if (!BindCol(1, SQL_INTEGER,  sizeof(player_id),    &player_id,    &cb_id))    return playerInfo;
	if (!BindCol(2, SQL_INTEGER,  sizeof(player_x),     &player_x,     &cb_x))     return playerInfo;
	if (!BindCol(3, SQL_INTEGER,  sizeof(player_y),     &player_y,     &cb_y))     return playerInfo;
	if (!BindCol(4, SQL_TINYINT,  sizeof(player_level), &player_level, &cb_level)) return playerInfo;
	if (!BindCol(5, SQL_INTEGER,  sizeof(player_exp),   &player_exp,   &cb_exp))   return playerInfo;
	// 리니지풍 컨텐츠 확장 컬럼 — ExtractPlayerInfo 프로시저가 6~8번째로 돌려준다.
	if (!BindCol(6, SQL_INTEGER,  sizeof(player_adena),     &player_adena,     &cb_adena))     return playerInfo;
	if (!BindCol(7, SQL_SMALLINT, sizeof(player_alignment), &player_alignment, &cb_alignment)) return playerInfo;
	if (!BindCol(8, SQL_SMALLINT, sizeof(player_pk),        &player_pk,        &cb_pk))        return playerInfo;
	if (!Fetch()) return playerInfo;

	playerInfo._playerId = static_cast<int>(player_id);
	playerInfo._name     = name;
	playerInfo._x        = player_x;
	playerInfo._y        = player_y;
	playerInfo._level    = static_cast<uint8>(player_level);
	playerInfo._exp      = static_cast<uint32>(player_exp);
	playerInfo._adena     = player_adena > 0 ? static_cast<uint32>(player_adena) : 0;
	playerInfo._alignment = static_cast<int16_t>(player_alignment);
	playerInfo._pkCount   = player_pk > 0 ? static_cast<uint16_t>(player_pk) : 0;

	return playerInfo;
}

bool DBConnection::SaveUserInfo(const string& name, short x, short y, uint8 level, uint32 exp)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC SavePlayerInfo ?, ?, ?, ?, ?";

	if (!BindParam(1, SQL_C_CHAR,     SQL_WVARCHAR, name.size(), (SQLPOINTER)name.c_str(), nullptr)) return false;
	if (!BindParam(2, SQL_C_SHORT,    SQL_INTEGER,  0,           (SQLPOINTER)&x,           nullptr)) return false;
	if (!BindParam(3, SQL_C_SHORT,    SQL_INTEGER,  0,           (SQLPOINTER)&y,           nullptr)) return false;
	if (!BindParam(4, SQL_C_UTINYINT, SQL_TINYINT,  0,           (SQLPOINTER)&level,       nullptr)) return false;
	if (!BindParam(5, SQL_C_ULONG,    SQL_INTEGER,  0,           (SQLPOINTER)&exp,         nullptr)) return false;
	return Execute(query.c_str());
}

vector<DB_ITEM_INFO> DBConnection::ExtractInventory(int playerId)
{
	vector<DB_ITEM_INFO> items;
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC GetInventory ?";

	SQLINTEGER  pid = playerId;
	SQLSMALLINT slotIndex{}, itemId{}, count{};
	SQLCHAR     equipped{};
	SQLCHAR     enchant{};
	SQLBIGINT   extra{};
	SQLLEN cb_slot{}, cb_item{}, cb_count{}, cb_equipped{}, cb_enchant{}, cb_extra{};

	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return items;
	if (!Execute(query.c_str())) return items;
	if (!BindCol(1, SQL_SMALLINT, sizeof(slotIndex), &slotIndex, &cb_slot))     return items;
	if (!BindCol(2, SQL_SMALLINT, sizeof(itemId),    &itemId,    &cb_item))     return items;
	if (!BindCol(3, SQL_SMALLINT, sizeof(count),     &count,     &cb_count))    return items;
	if (!BindCol(4, SQL_BIT,      sizeof(equipped),  &equipped,  &cb_equipped)) return items;
	if (!BindCol(5, SQL_TINYINT,  sizeof(enchant),   &enchant,   &cb_enchant))  return items;
	// V2: 희귀도·옵션(Inventory.extra BIGINT). NULL이면(마이그레이션 직후 등) 0으로 본다.
	if (!BindCol(6, SQL_C_SBIGINT, sizeof(extra),    &extra,     &cb_extra))    return items;

	// GetInventory는 슬롯 개수만큼 여러 행을 반환하므로, 다른 Extract*와 달리
	// Fetch()를 한 번이 아니라 SQL_NO_DATA가 나올 때까지 반복한다.
	while (Fetch())
	{
		DB_ITEM_INFO info{};
		info._slotIndex = static_cast<uint16_t>(slotIndex);
		info._itemId    = static_cast<uint16_t>(itemId);
		info._count     = static_cast<uint16_t>(count);
		info._equipped  = (equipped != 0);
		info._enchant   = static_cast<uint8_t>(enchant);
		info._extra     = (cb_extra == SQL_NULL_DATA || extra < 0) ? 0 : static_cast<uint64_t>(extra);
		items.push_back(info);
	}

	return items;
}

bool DBConnection::SaveInventorySlot(int playerId, uint16_t slotIndex, uint16_t itemId, uint16_t count, bool equipped, uint8_t enchant, uint64_t extra)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC SaveInventorySlot ?, ?, ?, ?, ?, ?, ?";

	SQLINTEGER  pid  = playerId;
	SQLSMALLINT slot = static_cast<SQLSMALLINT>(slotIndex);
	SQLSMALLINT item = static_cast<SQLSMALLINT>(itemId);
	SQLSMALLINT cnt  = static_cast<SQLSMALLINT>(count);
	SQLCHAR     eq   = equipped ? 1 : 0;
	SQLCHAR     enc  = enchant;
	SQLBIGINT   ext  = static_cast<SQLBIGINT>(extra & 0x3FFFFFFFFFFFFFFFull);   // 상위 2비트는 항상 0(ItemInstance)

	if (!BindParam(1, SQL_C_LONG,     SQL_INTEGER,  0, (SQLPOINTER)&pid,  nullptr)) return false;
	if (!BindParam(2, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&slot, nullptr)) return false;
	if (!BindParam(3, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&item, nullptr)) return false;
	if (!BindParam(4, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&cnt,  nullptr)) return false;
	if (!BindParam(5, SQL_C_BIT,      SQL_BIT,      0, (SQLPOINTER)&eq,   nullptr)) return false;
	if (!BindParam(6, SQL_C_UTINYINT, SQL_TINYINT,  0, (SQLPOINTER)&enc,  nullptr)) return false;
	if (!BindParam(7, SQL_C_SBIGINT,  SQL_BIGINT,   0, (SQLPOINTER)&ext,  nullptr)) return false;
	return Execute(query.c_str());
}

bool DBConnection::DeleteInventorySlot(int playerId, uint16_t slotIndex)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC DeleteInventorySlot ?, ?";

	SQLINTEGER  pid  = playerId;
	SQLSMALLINT slot = static_cast<SQLSMALLINT>(slotIndex);

	if (!BindParam(1, SQL_C_LONG,  SQL_INTEGER,  0, (SQLPOINTER)&pid,  nullptr)) return false;
	if (!BindParam(2, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&slot, nullptr)) return false;
	return Execute(query.c_str());
}

bool DBConnection::SaveTwoInventorySlots(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC SaveTwoInventorySlots ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?";

	SQLINTEGER  pid    = playerId;
	SQLSMALLINT slotA  = static_cast<SQLSMALLINT>(a.slotIndex);
	SQLCHAR     hasA   = a.hasItem ? 1 : 0;
	SQLSMALLINT itemA  = static_cast<SQLSMALLINT>(a.itemId);
	SQLSMALLINT cntA   = static_cast<SQLSMALLINT>(a.count);
	SQLCHAR     eqA    = a.equipped ? 1 : 0;
	SQLSMALLINT slotB  = static_cast<SQLSMALLINT>(b.slotIndex);
	SQLCHAR     hasB   = b.hasItem ? 1 : 0;
	SQLSMALLINT itemB  = static_cast<SQLSMALLINT>(b.itemId);
	SQLSMALLINT cntB   = static_cast<SQLSMALLINT>(b.count);
	SQLCHAR     eqB    = b.equipped ? 1 : 0;
	SQLCHAR     encA   = a.enchant;
	SQLCHAR     encB   = b.enchant;
	SQLBIGINT   extA   = static_cast<SQLBIGINT>(a.extra & 0x3FFFFFFFFFFFFFFFull);
	SQLBIGINT   extB   = static_cast<SQLBIGINT>(b.extra & 0x3FFFFFFFFFFFFFFFull);

	if (!BindParam(1,  SQL_C_LONG,  SQL_INTEGER,  0, (SQLPOINTER)&pid,   nullptr)) return false;
	if (!BindParam(2,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&slotA, nullptr)) return false;
	if (!BindParam(3,  SQL_C_BIT,   SQL_BIT,      0, (SQLPOINTER)&hasA,  nullptr)) return false;
	if (!BindParam(4,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&itemA, nullptr)) return false;
	if (!BindParam(5,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&cntA,  nullptr)) return false;
	if (!BindParam(6,  SQL_C_BIT,   SQL_BIT,      0, (SQLPOINTER)&eqA,   nullptr)) return false;
	if (!BindParam(7,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&slotB, nullptr)) return false;
	if (!BindParam(8,  SQL_C_BIT,   SQL_BIT,      0, (SQLPOINTER)&hasB,  nullptr)) return false;
	if (!BindParam(9,  SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&itemB, nullptr)) return false;
	if (!BindParam(10, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&cntB,  nullptr)) return false;
	if (!BindParam(11, SQL_C_BIT,   SQL_BIT,      0, (SQLPOINTER)&eqB,   nullptr)) return false;
	// 강화 수치는 프로시저 끝 두 파라미터(@enchantA, @enchantB)로 붙였다.
	if (!BindParam(12, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&encA, nullptr)) return false;
	if (!BindParam(13, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&encB, nullptr)) return false;
	// V2 희귀도·옵션은 그 뒤 두 파라미터(@extraA, @extraB)
	if (!BindParam(14, SQL_C_SBIGINT,  SQL_BIGINT,  0, (SQLPOINTER)&extA, nullptr)) return false;
	if (!BindParam(15, SQL_C_SBIGINT,  SQL_BIGINT,  0, (SQLPOINTER)&extB, nullptr)) return false;
	return Execute(query.c_str());
}

bool DBConnection::SavePlayerAdena(int playerId, uint32_t adena)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC SavePlayerAdena ?, ?";

	SQLINTEGER pid = playerId;
	SQLINTEGER amt = static_cast<SQLINTEGER>(adena);

	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&amt, nullptr)) return false;
	return Execute(query.c_str());
}

bool DBConnection::SavePlayerAlignment(int playerId, int16_t alignment, uint16_t pkCount)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC SavePlayerAlignment ?, ?, ?";

	SQLINTEGER  pid = playerId;
	SQLSMALLINT al  = alignment;
	SQLSMALLINT pk  = static_cast<SQLSMALLINT>(pkCount);

	if (!BindParam(1, SQL_C_LONG,  SQL_INTEGER,  0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!BindParam(2, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&al,  nullptr)) return false;
	if (!BindParam(3, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&pk,  nullptr)) return false;
	return Execute(query.c_str());
}

bool DBConnection::ShopTransaction(int playerId, uint32_t adena, const DB_ITEM_SLOT_SAVE& slot)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC ShopTransaction ?, ?, ?, ?, ?, ?, ?, ?";

	SQLINTEGER  pid   = playerId;
	SQLINTEGER  amt   = static_cast<SQLINTEGER>(adena);
	SQLSMALLINT idx   = static_cast<SQLSMALLINT>(slot.slotIndex);
	SQLCHAR     has   = slot.hasItem ? 1 : 0;
	SQLSMALLINT item  = static_cast<SQLSMALLINT>(slot.itemId);
	SQLSMALLINT cnt   = static_cast<SQLSMALLINT>(slot.count);
	SQLCHAR     eq    = slot.equipped ? 1 : 0;
	SQLCHAR     enc   = slot.enchant;

	if (!BindParam(1, SQL_C_LONG,     SQL_INTEGER,  0, (SQLPOINTER)&pid,  nullptr)) return false;
	if (!BindParam(2, SQL_C_LONG,     SQL_INTEGER,  0, (SQLPOINTER)&amt,  nullptr)) return false;
	if (!BindParam(3, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&idx,  nullptr)) return false;
	if (!BindParam(4, SQL_C_BIT,      SQL_BIT,      0, (SQLPOINTER)&has,  nullptr)) return false;
	if (!BindParam(5, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&item, nullptr)) return false;
	if (!BindParam(6, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&cnt,  nullptr)) return false;
	if (!BindParam(7, SQL_C_BIT,      SQL_BIT,      0, (SQLPOINTER)&eq,   nullptr)) return false;
	if (!BindParam(8, SQL_C_UTINYINT, SQL_TINYINT,  0, (SQLPOINTER)&enc,  nullptr)) return false;
	return Execute(query.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// V2 캐릭터 진행 데이터 — PlayerProgress(playerId, data VARBINARY(4000))
// ─────────────────────────────────────────────────────────────────────────────
bool DBConnection::LoadPlayerProgress(int playerId, std::vector<uint8_t>& out)
{
	out.clear();
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC LoadPlayerProgress ?";
	SQLINTEGER pid = playerId;
	std::vector<uint8_t> buffer(4000);
	SQLLEN cb_data{};
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	if (!BindCol(1, SQL_C_BINARY, buffer.size(), buffer.data(), &cb_data)) return false;
	if (!Fetch())
		return true;   // 행 없음 = 아직 저장한 적 없는 캐릭터(기본값으로 시작)
	if (cb_data > 0 && static_cast<size_t>(cb_data) <= buffer.size())
		out.assign(buffer.begin(), buffer.begin() + cb_data);
	return true;
}

bool DBConnection::SavePlayerProgress(int playerId, const std::vector<uint8_t>& data)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC SavePlayerProgress ?, ?";
	SQLINTEGER pid = playerId;
	SQLLEN cb_data = static_cast<SQLLEN>(data.size());
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!BindParam(2, SQL_C_BINARY, SQL_VARBINARY, 4000, (SQLPOINTER)data.data(), &cb_data)) return false;
	return Execute(query.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 2-D 기술 성장 — PlayerSkills(playerId, skillId, level, pick1, pick2)
// ─────────────────────────────────────────────────────────────────────────────
bool DBConnection::LoadPlayerSkills(int playerId, std::vector<DB_SKILL_INFO>& out)
{
	out.clear();
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC LoadPlayerSkills ?";
	SQLINTEGER  pid = playerId;
	SQLSMALLINT skillId{};
	SQLCHAR     level{}, pick1{}, pick2{};
	SQLLEN cb_id{}, cb_level{}, cb_p1{}, cb_p2{};
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	if (!BindCol(1, SQL_C_SHORT,    sizeof(skillId), &skillId, &cb_id))    return false;
	if (!BindCol(2, SQL_C_UTINYINT, sizeof(level),   &level,   &cb_level)) return false;
	if (!BindCol(3, SQL_C_UTINYINT, sizeof(pick1),   &pick1,   &cb_p1))    return false;
	if (!BindCol(4, SQL_C_UTINYINT, sizeof(pick2),   &pick2,   &cb_p2))    return false;
	while (Fetch())
	{
		DB_SKILL_INFO info{};
		info._skillId = static_cast<uint16_t>(skillId);
		info._level   = static_cast<uint8_t>(level);
		info._pick1   = static_cast<uint8_t>(pick1);
		info._pick2   = static_cast<uint8_t>(pick2);
		out.push_back(info);
	}
	return true;
}

bool DBConnection::SavePlayerSkill(int playerId, const DB_SKILL_INFO& skill)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC SavePlayerSkill ?, ?, ?, ?, ?";
	SQLINTEGER  pid   = playerId;
	SQLSMALLINT sid   = static_cast<SQLSMALLINT>(skill._skillId);
	SQLCHAR     level = skill._level;
	SQLCHAR     p1    = skill._pick1;
	SQLCHAR     p2    = skill._pick2;
	if (!BindParam(1, SQL_C_LONG,     SQL_INTEGER,  0, (SQLPOINTER)&pid,   nullptr)) return false;
	if (!BindParam(2, SQL_C_SHORT,    SQL_SMALLINT, 0, (SQLPOINTER)&sid,   nullptr)) return false;
	if (!BindParam(3, SQL_C_UTINYINT, SQL_TINYINT,  0, (SQLPOINTER)&level, nullptr)) return false;
	if (!BindParam(4, SQL_C_UTINYINT, SQL_TINYINT,  0, (SQLPOINTER)&p1,    nullptr)) return false;
	if (!BindParam(5, SQL_C_UTINYINT, SQL_TINYINT,  0, (SQLPOINTER)&p2,    nullptr)) return false;
	return Execute(query.c_str());
}

bool DBConnection::ResetPlayerSkills(int playerId)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC ResetPlayerSkills ?";
	SQLINTEGER pid = playerId;
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	return Execute(query.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사 · 경매장(docs/adr/0001-guild-auction-services.md, Database/setup.sql 5~6장)
// 프로시저는 결과 한 줄(ok BIT)을 돌려준다 — 상태 검사에 걸리면 0이고 아무것도 바뀌지 않는다(XACT_ABORT + 한 트랜잭션).
// 결사 이름 · 공지는 한글이라 UTF-8 → UTF-16으로 바꿔 NVARCHAR로 넘기고, 읽을 때 되돌린다.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	std::wstring Utf8ToWide(const string& s)
	{
		if (s.empty()) return {};
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
		if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
		return w;
	}
	string WideToUtf8(const wchar_t* w, SQLLEN bytes)
	{
		if (w == nullptr || bytes <= 0) return {};
		const int chars = static_cast<int>(bytes / static_cast<SQLLEN>(sizeof(wchar_t)));
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, chars, nullptr, 0, nullptr, nullptr);
		string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
		if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, w, chars, s.data(), n, nullptr, nullptr);
		return s;
	}
}

// 결과 한 줄(ok BIT)을 읽는다 — 프로시저가 SELECT CAST(@ok AS BIT)로 끝난다
#define PHASE5_FETCH_OK()                                                          \
	do {                                                                           \
		SQLCHAR ok_{}; SQLLEN cb_ok_{};                                            \
		if (!BindCol(1, SQL_C_BIT, sizeof(ok_), &ok_, &cb_ok_)) return false;      \
		if (!Fetch()) return false;                                                \
		return ok_ != 0;                                                           \
	} while (false)

bool DBConnection::LoadGuilds(vector<DB_GUILD_ROW>& out)
{
	out.clear();
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC LoadGuilds";
	SQLINTEGER gid{}, leader{};
	SQLCHAR emblem{};
	wchar_t name[32]{}, notice[48]{};
	SQLLEN cb_gid{}, cb_name{}, cb_emblem{}, cb_notice{}, cb_leader{};
	if (!Execute(query.c_str())) return false;
	if (!BindCol(1, SQL_C_LONG, sizeof(gid), &gid, &cb_gid)) return false;
	if (!BindCol(2, SQL_C_WCHAR, sizeof(name), name, &cb_name)) return false;
	if (!BindCol(3, SQL_C_UTINYINT, sizeof(emblem), &emblem, &cb_emblem)) return false;
	if (!BindCol(4, SQL_C_WCHAR, sizeof(notice), notice, &cb_notice)) return false;
	if (!BindCol(5, SQL_C_LONG, sizeof(leader), &leader, &cb_leader)) return false;
	while (Fetch())
	{
		DB_GUILD_ROW r;
		r.guildId = static_cast<int>(gid);
		r.name = WideToUtf8(name, cb_name);
		r.emblem = static_cast<uint8_t>(emblem);
		r.notice = cb_notice == SQL_NULL_DATA ? string() : WideToUtf8(notice, cb_notice);
		r.leaderId = static_cast<int>(leader);
		out.push_back(std::move(r));
	}
	return true;
}

bool DBConnection::LoadGuildMembers(vector<DB_GUILD_MEMBER_ROW>& out)
{
	out.clear();
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC LoadGuildMembers";
	SQLINTEGER gid{}, pid{};
	SQLCHAR rank{}, level{};
	char name[NAME_SIZE + 1]{};
	SQLBIGINT seen{};
	SQLLEN cb_gid{}, cb_pid{}, cb_rank{}, cb_name{}, cb_level{}, cb_seen{};
	if (!Execute(query.c_str())) return false;
	if (!BindCol(1, SQL_C_LONG, sizeof(gid), &gid, &cb_gid)) return false;
	if (!BindCol(2, SQL_C_LONG, sizeof(pid), &pid, &cb_pid)) return false;
	if (!BindCol(3, SQL_C_UTINYINT, sizeof(rank), &rank, &cb_rank)) return false;
	if (!BindCol(4, SQL_C_CHAR, sizeof(name), name, &cb_name)) return false;
	if (!BindCol(5, SQL_C_UTINYINT, sizeof(level), &level, &cb_level)) return false;
	if (!BindCol(6, SQL_C_SBIGINT, sizeof(seen), &seen, &cb_seen)) return false;
	while (Fetch())
	{
		DB_GUILD_MEMBER_ROW r;
		r.guildId = static_cast<int>(gid);
		r.playerId = static_cast<int>(pid);
		r.rank = static_cast<uint8_t>(rank);
		r.name = name;
		r.level = static_cast<uint8_t>(level);
		r.lastSeenMin = (cb_seen == SQL_NULL_DATA || seen <= 0) ? 0u : static_cast<uint32_t>(seen / 60);
		out.push_back(std::move(r));
	}
	return true;
}

bool DBConnection::GuildCreate(int playerId, uint32_t adenaAfter, int guildId, const string& name, const string& nameKey, uint8_t emblem)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC GuildCreate ?, ?, ?, ?, ?, ?";
	SQLINTEGER pid = playerId, gid = guildId;
	SQLINTEGER adena = static_cast<SQLINTEGER>(adenaAfter);
	SQLCHAR em = emblem;
	const std::wstring wname = Utf8ToWide(name), wkey = Utf8ToWide(nameKey);
	SQLLEN cb_name = static_cast<SQLLEN>(wname.size() * sizeof(wchar_t)), cb_key = static_cast<SQLLEN>(wkey.size() * sizeof(wchar_t));
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&adena, nullptr)) return false;
	if (!BindParam(3, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
	if (!BindParam(4, SQL_C_WCHAR, SQL_WVARCHAR, 16, (SQLPOINTER)wname.c_str(), &cb_name)) return false;
	if (!BindParam(5, SQL_C_WCHAR, SQL_WVARCHAR, 16, (SQLPOINTER)wkey.c_str(), &cb_key)) return false;
	if (!BindParam(6, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&em, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::GuildMemberSet(int guildId, int playerId, uint8_t rank)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC GuildMemberSet ?, ?, ?";
	SQLINTEGER gid = guildId, pid = playerId;
	SQLCHAR rk = rank;
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
	if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!BindParam(3, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&rk, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::GuildMemberRemove(int guildId, int playerId)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC GuildMemberRemove ?, ?";
	SQLINTEGER gid = guildId, pid = playerId;
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
	if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::GuildTransfer(int guildId, int fromId, int toId)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC GuildTransfer ?, ?, ?";
	SQLINTEGER gid = guildId, a = fromId, b = toId;
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
	if (!BindParam(2, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&a, nullptr)) return false;
	if (!BindParam(3, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&b, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::GuildSetInfo(int guildId, uint8_t emblem, const string& notice)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC GuildSetInfo ?, ?, ?";
	SQLINTEGER gid = guildId;
	SQLCHAR em = emblem;
	const std::wstring wnotice = Utf8ToWide(notice);
	SQLLEN cb_notice = static_cast<SQLLEN>(wnotice.size() * sizeof(wchar_t));
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
	if (!BindParam(2, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&em, nullptr)) return false;
	if (!BindParam(3, SQL_C_WCHAR, SQL_WVARCHAR, 40, (SQLPOINTER)wnotice.c_str(), &cb_notice)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::GuildDisband(int guildId)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC GuildDisband ?";
	SQLINTEGER gid = guildId;
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gid, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::GuildMemberSeen(int playerId, int64_t unixSec)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC GuildMemberSeen ?, ?";
	SQLINTEGER pid = playerId;
	SQLBIGINT t = static_cast<SQLBIGINT>(unixSec);
	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!BindParam(2, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&t, nullptr)) return false;
	return Execute(query.c_str());
}

namespace
{
	// 목록 · 정산함 행을 읽는 공통 열 묶음
	struct ItemCols
	{
		SQLSMALLINT itemId{}, count{};
		SQLCHAR     enchant{};
		SQLBIGINT   extra{};
		SQLLEN cb_item{}, cb_count{}, cb_enchant{}, cb_extra{};
	};
}

bool DBConnection::LoadAuctionListings(vector<DB_LISTING_ROW>& out)
{
	out.clear();
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC LoadAuctionListings";
	SQLBIGINT lid{}, created{}, expires{};
	SQLINTEGER seller{}, price{}, deposit{};
	ItemCols ic;
	SQLLEN cb_lid{}, cb_seller{}, cb_price{}, cb_dep{}, cb_created{}, cb_expires{};
	if (!Execute(query.c_str())) return false;
	if (!BindCol(1, SQL_C_SBIGINT, sizeof(lid), &lid, &cb_lid)) return false;
	if (!BindCol(2, SQL_C_LONG, sizeof(seller), &seller, &cb_seller)) return false;
	if (!BindCol(3, SQL_C_SHORT, sizeof(ic.itemId), &ic.itemId, &ic.cb_item)) return false;
	if (!BindCol(4, SQL_C_SHORT, sizeof(ic.count), &ic.count, &ic.cb_count)) return false;
	if (!BindCol(5, SQL_C_UTINYINT, sizeof(ic.enchant), &ic.enchant, &ic.cb_enchant)) return false;
	if (!BindCol(6, SQL_C_SBIGINT, sizeof(ic.extra), &ic.extra, &ic.cb_extra)) return false;
	if (!BindCol(7, SQL_C_LONG, sizeof(price), &price, &cb_price)) return false;
	if (!BindCol(8, SQL_C_LONG, sizeof(deposit), &deposit, &cb_dep)) return false;
	if (!BindCol(9, SQL_C_SBIGINT, sizeof(created), &created, &cb_created)) return false;
	if (!BindCol(10, SQL_C_SBIGINT, sizeof(expires), &expires, &cb_expires)) return false;
	while (Fetch())
	{
		DB_LISTING_ROW r;
		r.listingId = static_cast<int64_t>(lid);
		r.sellerId = static_cast<int>(seller);
		r.itemId = static_cast<uint16_t>(ic.itemId);
		r.count = static_cast<uint16_t>(ic.count);
		r.enchant = static_cast<uint8_t>(ic.enchant);
		r.extra = ic.extra < 0 ? 0 : static_cast<uint64_t>(ic.extra);
		r.price = static_cast<uint32_t>(price);
		r.deposit = static_cast<uint32_t>(deposit);
		r.createdAt = static_cast<int64_t>(created);
		r.expiresAt = static_cast<int64_t>(expires);
		r.status = 0;
		out.push_back(r);
	}
	return true;
}

bool DBConnection::LoadAuctionSettlements(vector<DB_SETTLEMENT_ROW>& out)
{
	out.clear();
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC LoadAuctionSettlements";
	SQLBIGINT sid{}, lid{}, created{};
	SQLINTEGER pid{}, adena{};
	SQLCHAR kind{}, reason{};
	ItemCols ic;
	SQLLEN cb_sid{}, cb_pid{}, cb_kind{}, cb_adena{}, cb_lid{}, cb_reason{}, cb_created{};
	if (!Execute(query.c_str())) return false;
	if (!BindCol(1, SQL_C_SBIGINT, sizeof(sid), &sid, &cb_sid)) return false;
	if (!BindCol(2, SQL_C_LONG, sizeof(pid), &pid, &cb_pid)) return false;
	if (!BindCol(3, SQL_C_UTINYINT, sizeof(kind), &kind, &cb_kind)) return false;
	if (!BindCol(4, SQL_C_LONG, sizeof(adena), &adena, &cb_adena)) return false;
	if (!BindCol(5, SQL_C_SHORT, sizeof(ic.itemId), &ic.itemId, &ic.cb_item)) return false;
	if (!BindCol(6, SQL_C_SHORT, sizeof(ic.count), &ic.count, &ic.cb_count)) return false;
	if (!BindCol(7, SQL_C_UTINYINT, sizeof(ic.enchant), &ic.enchant, &ic.cb_enchant)) return false;
	if (!BindCol(8, SQL_C_SBIGINT, sizeof(ic.extra), &ic.extra, &ic.cb_extra)) return false;
	if (!BindCol(9, SQL_C_SBIGINT, sizeof(lid), &lid, &cb_lid)) return false;
	if (!BindCol(10, SQL_C_UTINYINT, sizeof(reason), &reason, &cb_reason)) return false;
	if (!BindCol(11, SQL_C_SBIGINT, sizeof(created), &created, &cb_created)) return false;
	while (Fetch())
	{
		DB_SETTLEMENT_ROW r;
		r.settlementId = static_cast<int64_t>(sid);
		r.playerId = static_cast<int>(pid);
		r.kind = static_cast<uint8_t>(kind);
		r.adena = static_cast<uint32_t>(adena);
		r.itemId = static_cast<uint16_t>(ic.itemId);
		r.count = static_cast<uint16_t>(ic.count);
		r.enchant = static_cast<uint8_t>(ic.enchant);
		r.extra = ic.extra < 0 ? 0 : static_cast<uint64_t>(ic.extra);
		r.listingId = static_cast<int64_t>(lid);
		r.reason = static_cast<uint8_t>(reason);
		r.createdAt = static_cast<int64_t>(created);
		out.push_back(r);
	}
	return true;
}

bool DBConnection::LoadAuctionPrices(vector<DB_PRICE_ROW>& out, int64_t sinceUnix)
{
	out.clear();
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC LoadAuctionPrices ?";
	SQLBIGINT since = static_cast<SQLBIGINT>(sinceUnix), soldAt{};
	SQLSMALLINT itemId{};
	SQLINTEGER price{};
	SQLLEN cb_item{}, cb_price{}, cb_sold{};
	if (!BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&since, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	if (!BindCol(1, SQL_C_SHORT, sizeof(itemId), &itemId, &cb_item)) return false;
	if (!BindCol(2, SQL_C_LONG, sizeof(price), &price, &cb_price)) return false;
	if (!BindCol(3, SQL_C_SBIGINT, sizeof(soldAt), &soldAt, &cb_sold)) return false;
	while (Fetch())
		out.push_back(DB_PRICE_ROW{ static_cast<uint16_t>(itemId), static_cast<uint32_t>(price), static_cast<int64_t>(soldAt) });
	return true;
}

bool DBConnection::AuctionMaxIds(int64_t& maxListing, int64_t& maxSettlement, int& maxGuild)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC AuctionMaxIds";
	SQLBIGINT ml{}, ms{};
	SQLINTEGER mg{};
	SQLLEN cb1{}, cb2{}, cb3{};
	if (!Execute(query.c_str())) return false;
	if (!BindCol(1, SQL_C_SBIGINT, sizeof(ml), &ml, &cb1)) return false;
	if (!BindCol(2, SQL_C_SBIGINT, sizeof(ms), &ms, &cb2)) return false;
	if (!BindCol(3, SQL_C_LONG, sizeof(mg), &mg, &cb3)) return false;
	if (!Fetch()) return false;
	maxListing = static_cast<int64_t>(ml);
	maxSettlement = static_cast<int64_t>(ms);
	maxGuild = static_cast<int>(mg);
	return true;
}

bool DBConnection::AuctionRegister(int sellerId, uint32_t adenaAfter, const DB_ITEM_SLOT_SAVE& slot, const DB_LISTING_ROW& l)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC AuctionRegister ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?";
	SQLINTEGER  pid = sellerId, adena = static_cast<SQLINTEGER>(adenaAfter);
	SQLSMALLINT sIdx = static_cast<SQLSMALLINT>(slot.slotIndex), sItem = static_cast<SQLSMALLINT>(slot.itemId), sCnt = static_cast<SQLSMALLINT>(slot.count);
	SQLCHAR     sHas = slot.hasItem ? 1 : 0, sEq = slot.equipped ? 1 : 0, sEnc = slot.enchant;
	SQLBIGINT   sExt = static_cast<SQLBIGINT>(slot.extra & 0x3FFFFFFFFFFFFFFFull);
	SQLBIGINT   lid = static_cast<SQLBIGINT>(l.listingId), created = static_cast<SQLBIGINT>(l.createdAt), expires = static_cast<SQLBIGINT>(l.expiresAt);
	SQLSMALLINT lItem = static_cast<SQLSMALLINT>(l.itemId), lCnt = static_cast<SQLSMALLINT>(l.count);
	SQLCHAR     lEnc = l.enchant;
	SQLBIGINT   lExt = static_cast<SQLBIGINT>(l.extra & 0x3FFFFFFFFFFFFFFFull);
	SQLINTEGER  price = static_cast<SQLINTEGER>(l.price), deposit = static_cast<SQLINTEGER>(l.deposit);
	int i = 1;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&adena, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sIdx, nullptr)) return false;
	if (!BindParam(i++, SQL_C_BIT, SQL_BIT, 0, (SQLPOINTER)&sHas, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sItem, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sCnt, nullptr)) return false;
	if (!BindParam(i++, SQL_C_BIT, SQL_BIT, 0, (SQLPOINTER)&sEq, nullptr)) return false;
	if (!BindParam(i++, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&sEnc, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&sExt, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&lid, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&lItem, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&lCnt, nullptr)) return false;
	if (!BindParam(i++, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&lEnc, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&lExt, nullptr)) return false;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&price, nullptr)) return false;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&deposit, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&created, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&expires, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::AuctionBuy(int buyerId, uint32_t adenaAfter, int64_t listingId, uint32_t price, int64_t buyerSettleId, int64_t sellerSettleId, uint32_t sellerGets, int64_t nowUnix)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC AuctionBuy ?, ?, ?, ?, ?, ?, ?, ?";
	SQLINTEGER pid = buyerId, adena = static_cast<SQLINTEGER>(adenaAfter), pr = static_cast<SQLINTEGER>(price), gets = static_cast<SQLINTEGER>(sellerGets);
	SQLBIGINT lid = static_cast<SQLBIGINT>(listingId), bs = static_cast<SQLBIGINT>(buyerSettleId), ss = static_cast<SQLBIGINT>(sellerSettleId), now = static_cast<SQLBIGINT>(nowUnix);
	int i = 1;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&adena, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&lid, nullptr)) return false;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pr, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&bs, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&ss, nullptr)) return false;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&gets, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&now, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::AuctionClose(int64_t listingId, uint8_t status, int64_t settleId, int64_t nowUnix)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC AuctionClose ?, ?, ?, ?";
	SQLBIGINT lid = static_cast<SQLBIGINT>(listingId), sid = static_cast<SQLBIGINT>(settleId), now = static_cast<SQLBIGINT>(nowUnix);
	SQLCHAR st = status;
	if (!BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&lid, nullptr)) return false;
	if (!BindParam(2, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&st, nullptr)) return false;
	if (!BindParam(3, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&sid, nullptr)) return false;
	if (!BindParam(4, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&now, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::SettlementClaim(int playerId, int64_t settlementId, uint32_t adenaAfter, const DB_ITEM_SLOT_SAVE& slot, int64_t nowUnix)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC SettlementClaim ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?";
	SQLINTEGER  pid = playerId, adena = static_cast<SQLINTEGER>(adenaAfter);
	SQLBIGINT   sid = static_cast<SQLBIGINT>(settlementId), now = static_cast<SQLBIGINT>(nowUnix);
	SQLSMALLINT sIdx = static_cast<SQLSMALLINT>(slot.slotIndex), sItem = static_cast<SQLSMALLINT>(slot.itemId), sCnt = static_cast<SQLSMALLINT>(slot.count);
	SQLCHAR     sHas = slot.hasItem ? 1 : 0, sEq = slot.equipped ? 1 : 0, sEnc = slot.enchant;
	SQLBIGINT   sExt = static_cast<SQLBIGINT>(slot.extra & 0x3FFFFFFFFFFFFFFFull);
	int i = 1;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&sid, nullptr)) return false;
	if (!BindParam(i++, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&adena, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sIdx, nullptr)) return false;
	if (!BindParam(i++, SQL_C_BIT, SQL_BIT, 0, (SQLPOINTER)&sHas, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sItem, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&sCnt, nullptr)) return false;
	if (!BindParam(i++, SQL_C_BIT, SQL_BIT, 0, (SQLPOINTER)&sEq, nullptr)) return false;
	if (!BindParam(i++, SQL_C_UTINYINT, SQL_TINYINT, 0, (SQLPOINTER)&sEnc, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&sExt, nullptr)) return false;
	if (!BindParam(i++, SQL_C_SBIGINT, SQL_BIGINT, 0, (SQLPOINTER)&now, nullptr)) return false;
	if (!Execute(query.c_str())) return false;
	PHASE5_FETCH_OK();
}

bool DBConnection::AuditTotals(uint64_t& walletAdena, uint64_t& settleAdena, uint64_t& invUnits, uint64_t& listingUnits, uint64_t& settleUnits, uint64_t& feesAndDeposits)
{
	StatementCleanup cleanup(*this);
	wstring query = L"EXEC AuctionAudit";
	SQLBIGINT a{}, b{}, c{}, d{}, e{}, f{};
	SQLLEN cb[6]{};
	if (!Execute(query.c_str())) return false;
	if (!BindCol(1, SQL_C_SBIGINT, sizeof(a), &a, &cb[0])) return false;
	if (!BindCol(2, SQL_C_SBIGINT, sizeof(b), &b, &cb[1])) return false;
	if (!BindCol(3, SQL_C_SBIGINT, sizeof(c), &c, &cb[2])) return false;
	if (!BindCol(4, SQL_C_SBIGINT, sizeof(d), &d, &cb[3])) return false;
	if (!BindCol(5, SQL_C_SBIGINT, sizeof(e), &e, &cb[4])) return false;
	if (!BindCol(6, SQL_C_SBIGINT, sizeof(f), &f, &cb[5])) return false;
	if (!Fetch()) return false;
	walletAdena = static_cast<uint64_t>(a); settleAdena = static_cast<uint64_t>(b); invUnits = static_cast<uint64_t>(c);
	listingUnits = static_cast<uint64_t>(d); settleUnits = static_cast<uint64_t>(e); feesAndDeposits = static_cast<uint64_t>(f);
	return true;
}
#undef PHASE5_FETCH_OK
