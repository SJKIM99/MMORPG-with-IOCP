#pragma once

#define WIN32_LEAN_AND_MEAN // 거의 사용되지 않는 내용을 Windows 헤더에서 제외합니다.

#include "Core\\GameServerCore.h"
#include "Protocol.h"
#include "ServerGlobal.h"

enum IO_TYPE
{
	IO_ACCEPT,
	IO_RECV,
	IO_SEND,
};

enum SOCKET_STATE
{
	ST_FREE,
	ST_ALLOC,
	ST_INGAME
};
