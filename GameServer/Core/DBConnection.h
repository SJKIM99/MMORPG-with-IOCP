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
	bool			AddUserInfoInDataBase(const string& name, const string& password, short x, short y, uint8 level = 1, uint32 exp = 0);
	DB_USER_INFO	ExtractUserInfo(const string& name);
	bool			SaveUserInfo(const string& name, short x, short y, uint8 level, uint32 exp);

private:
	SQLHENV			_enviroment = SQL_NULL_HANDLE;
	SQLHDBC			_connection = SQL_NULL_HANDLE;
	SQLHSTMT		_statement = SQL_NULL_HANDLE;
	SQLRETURN		_retcode = SQL_SUCCESS;
};
