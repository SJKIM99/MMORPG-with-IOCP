#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 서버와 공유하는 헤더를 한 곳에서 include 한다.
//
// 서버의 Protocol.h/EnumCategory.h는 서버 pch.h(Windows 헤더 포함) 뒤에 오는 것을
// 전제로 쓰여 있어서 USHORT 같은 Windows 타입과 표준 헤더가 먼저 필요하다.
// 클라이언트는 raylib과 windows.h가 충돌(Rectangle/DrawText 등)하므로 windows.h를
// 끌어오지 않고, 필요한 것만 여기서 채운 뒤 서버 헤더를 그대로 include 한다.
// → 패킷 구조/아이템·몬스터·월드 데이터가 서버와 "같은 파일"이라 어긋날 수 없다.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <type_traits>
#include <utility>

#ifndef _WINDEF_
using USHORT = unsigned short;
#endif

#include "Protocol.h"                 // GameServer/Protocol.h
#include "Item/ItemTableData.h"       // 아이템 표(이름/가격/아이콘)
#include "Data/MonsterTable.h"        // 몬스터 종족 표
#include "Data/WorldData.h"           // 지역/마을/NPC/상점/충돌
#include "Data/LevelTable.h"          // 레벨/강화/성향치 규칙
#include "Data/ItemRules.h"           // 희귀도·옵션·세트(아이템 성장)
#include "Data/SkillData.h"           // Phase 2-B 기술 구조 V3(칸 · 유형 · 타격 시각표)
#include "Data/SkillGrowth.h"         // Phase 2-D 기술 성장(레벨 · 점수 · 갈래)
#include "Data/GuildAuction.h"        // Phase 5 결사 · 경매장 규칙(직위 · 권한 · 문장 · 수수료 · 분류)
#include "Zone/ZoneLayout.h"          // Zone(=지역) 좌표 계산

namespace net
{
	// ObjID를 unordered_map 키로 쓰기 위한 도우미(서버 헤더에 std::hash<ObjID>가 이미 있다)
	inline bool IsUser(const ObjID& id)    { return id.GetCategory() == EnumCategory::eUser; }
	inline bool IsMonster(const ObjID& id) { return id.GetCategory() == EnumCategory::eMonster; }
	inline bool IsItem(const ObjID& id)    { return id.GetCategory() == EnumCategory::eItem; }
}
