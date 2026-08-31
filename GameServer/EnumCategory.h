#pragma once

enum EnumCategory : USHORT
{
	eNone = 0,
	eAccount = 1,
	eUser = 2,
	eMonster = 3,
	eStat = 4,
	eUserLv = 5,
	eHp = 6,
	eExp= 7,
	eItem = 8,   // 인벤토리/필드 아이템 ObjID의 카테고리. KIND에 ItemTableId를 싣는다.
};