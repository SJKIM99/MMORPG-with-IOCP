#pragma once

#include "net/Shared.h"

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 화면 문구. 서버는 코드값만 보내고(SystemMessageCode 등), 문장은 전부 클라이언트가
// 만든다. 한국어 조사(을/를, 이/가, 은/는, 으로/로)는 앞 글자 받침에 맞춰 고른다.
// ─────────────────────────────────────────────────────────────────────────────
namespace texts
{
	// word 뒤에 붙일 조사. withBatchim/withoutBatchim 예: ("을", "를")
	std::string Josa(const std::string& word, const char* withBatchim, const char* withoutBatchim);
	// "으로/로" (ㄹ 받침은 "로")
	std::string JosaRo(const std::string& word);

	std::string ItemName(uint16_t itemId);
	std::string ItemNameEnchanted(uint16_t itemId, int enchant);
	std::string RegionName(int zoneId);
	std::string MonsterName(uint16_t species);

	// 시스템 메시지 문장. important = 화면 가운데에도 띄울 만한가
	std::string SystemMessage(SystemMessageCode code, int32_t p1, int32_t p2, bool& important);
	std::string WorldNotice(WorldNoticeCode code, int32_t p1, int32_t p2, const std::string& name);
	std::string ShopResultText(ShopKind kind, ShopResult result);

	std::string Number(long long v);          // 1,234
	std::string Stars(int tier);              // ★★★☆☆
}
