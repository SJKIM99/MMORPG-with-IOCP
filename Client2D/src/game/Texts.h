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

	// ── V2 아이템 성장 ──
	const char* SlotName(EquipSlot slot);            // "무기", "투구" ...
	const char* WeaponKindName(WeaponKind kind);     // "한손검", "지팡이" ...
	std::string OptionText(const OptionRoll& o);     // "치명타 확률 +1.2%", "방패 강타 피해 +30%"
	std::string ProcText(ItemProc p, int power);     // 특수 효과 설명(확률·수치 포함)
	// 이름(희귀도 접두 없이) — 신화·전설은 이름 앞에 ◆
	std::string ItemNameFull(uint16_t itemId, int enchant, uint64_t extra);

	std::string Number(long long v);          // 1,234
	std::string Stars(int tier);              // ★★★☆☆
}
