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
	SQLLEN cb_id{}, cb_x{}, cb_y{}, cb_level{}, cb_exp{};

	if (!BindParam(1, SQL_C_CHAR, SQL_WVARCHAR, name.size(), (SQLPOINTER)name.c_str(), nullptr)) return playerInfo;
	if (!Execute(query.c_str())) return playerInfo;
	if (!BindCol(1, SQL_INTEGER,  sizeof(player_id),    &player_id,    &cb_id))    return playerInfo;
	if (!BindCol(2, SQL_INTEGER,  sizeof(player_x),     &player_x,     &cb_x))     return playerInfo;
	if (!BindCol(3, SQL_INTEGER,  sizeof(player_y),     &player_y,     &cb_y))     return playerInfo;
	if (!BindCol(4, SQL_TINYINT,  sizeof(player_level), &player_level, &cb_level)) return playerInfo;
	if (!BindCol(5, SQL_INTEGER,  sizeof(player_exp),   &player_exp,   &cb_exp))   return playerInfo;
	if (!Fetch()) return playerInfo;

	playerInfo._playerId = static_cast<int>(player_id);
	playerInfo._name     = name;
	playerInfo._x        = player_x;
	playerInfo._y        = player_y;
	playerInfo._level    = static_cast<uint8>(player_level);
	playerInfo._exp      = static_cast<uint32>(player_exp);

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
	SQLLEN cb_slot{}, cb_item{}, cb_count{}, cb_equipped{};

	if (!BindParam(1, SQL_C_LONG, SQL_INTEGER, 0, (SQLPOINTER)&pid, nullptr)) return items;
	if (!Execute(query.c_str())) return items;
	if (!BindCol(1, SQL_SMALLINT, sizeof(slotIndex), &slotIndex, &cb_slot))     return items;
	if (!BindCol(2, SQL_SMALLINT, sizeof(itemId),    &itemId,    &cb_item))     return items;
	if (!BindCol(3, SQL_SMALLINT, sizeof(count),     &count,     &cb_count))    return items;
	if (!BindCol(4, SQL_BIT,      sizeof(equipped),  &equipped,  &cb_equipped)) return items;

	// GetInventory는 슬롯 개수만큼 여러 행을 반환하므로, 다른 Extract*와 달리
	// Fetch()를 한 번이 아니라 SQL_NO_DATA가 나올 때까지 반복한다.
	while (Fetch())
	{
		DB_ITEM_INFO info{};
		info._slotIndex = static_cast<uint16_t>(slotIndex);
		info._itemId    = static_cast<uint16_t>(itemId);
		info._count     = static_cast<uint16_t>(count);
		info._equipped  = (equipped != 0);
		items.push_back(info);
	}

	return items;
}

bool DBConnection::SaveInventorySlot(int playerId, uint16_t slotIndex, uint16_t itemId, uint16_t count, bool equipped)
{
	StatementCleanup cleanup(*this);

	wstring query = L"EXEC SaveInventorySlot ?, ?, ?, ?, ?";

	SQLINTEGER  pid  = playerId;
	SQLSMALLINT slot = static_cast<SQLSMALLINT>(slotIndex);
	SQLSMALLINT item = static_cast<SQLSMALLINT>(itemId);
	SQLSMALLINT cnt  = static_cast<SQLSMALLINT>(count);
	SQLCHAR     eq   = equipped ? 1 : 0;

	if (!BindParam(1, SQL_C_LONG,  SQL_INTEGER,  0, (SQLPOINTER)&pid,  nullptr)) return false;
	if (!BindParam(2, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&slot, nullptr)) return false;
	if (!BindParam(3, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&item, nullptr)) return false;
	if (!BindParam(4, SQL_C_SHORT, SQL_SMALLINT, 0, (SQLPOINTER)&cnt,  nullptr)) return false;
	if (!BindParam(5, SQL_C_BIT,   SQL_BIT,      0, (SQLPOINTER)&eq,   nullptr)) return false;
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

	wstring query = L"EXEC SaveTwoInventorySlots ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?";

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
	return Execute(query.c_str());
}
