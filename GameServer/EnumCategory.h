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
	// 인벤토리/필드 아이템 ObjID의 카테고리. DatabaseID는 ItemTableId가 아니라
	// 인스턴스별로 유일한 일련번호다(ItemHelper.cpp의 전용 카운터가 발급) —
	// 같은 itemId를 가진 아이템이 필드에 여러 개 동시에 있을 수 있어서, 아이템
	// 종류가 아니라 "그 아이템 하나하나"를 구분할 값이 필요하다.
	eItem = 8,
};