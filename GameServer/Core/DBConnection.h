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
	bool			IsPlayerRegistered(const string& name);
	bool			VerifyPlayerPassword(const string& name, const string& password);
	bool			AddPlayerInfoInDataBase(const string& name, const string& password, short x, short y);
	DB_PLAYER_INFO	ExtractPlayerInfo(const string& name);
	bool			SavePlayerInfo(const string& name, short x, short y);

private:
	SQLHENV			_enviroment = SQL_NULL_HANDLE;
	SQLHDBC			_connection = SQL_NULL_HANDLE;
	SQLHSTMT		_statement = SQL_NULL_HANDLE;
	SQLRETURN		_retcode = SQL_SUCCESS;
};
