#include "game/Texts.h"
#include "ui/Font.h"
#include "core/Util.h"

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace texts
{
	namespace
	{
		// 마지막 글자의 받침(종성) 번호. 한글이 아니면 -1, 받침 없음 0
		int LastJong(const std::string& word)
		{
			std::vector<int> cps;
			ui::DecodeUtf8(word, cps);
			for (auto it = cps.rbegin(); it != cps.rend(); ++it)
			{
				const int cp = *it;
				if (cp == ' ' || cp == ')' || cp == ']') continue;
				if (cp >= 0xAC00 && cp <= 0xD7A3)
					return (cp - 0xAC00) % 28;
				// 숫자/영문: 읽는 소리로 대충 판정(1,3,6,7,8,0 → 받침 있음)
				if (cp >= '0' && cp <= '9')
				{
					static const int digit[10] = { 1, 8, 0, 1, 0, 0, 1, 8, 1, 0 };
					return digit[cp - '0'];
				}
				if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
				{
					const char c = static_cast<char>(cp | 0x20);
					return (c == 'l' || c == 'm' || c == 'n' || c == 'r') ? (c == 'l' || c == 'r' ? 8 : 4) : 0;
				}
				return -1;
			}
			return -1;
		}

		std::string Fmt(const char* f, ...)
		{
			char buf[512];
			va_list ap;
			va_start(ap, f);
			std::vsnprintf(buf, sizeof(buf), f, ap);
			va_end(ap);
			return buf;
		}
	}

	std::string Josa(const std::string& word, const char* withBatchim, const char* withoutBatchim)
	{
		const int j = LastJong(word);
		return word + (j > 0 ? withBatchim : withoutBatchim);
	}

	std::string JosaRo(const std::string& word)
	{
		const int j = LastJong(word);
		return word + ((j > 0 && j != 8) ? "으로" : "로");
	}

	std::string ItemName(uint16_t itemId)
	{
		const ItemTableRow* row = ItemTableData::Find(itemId);
		return row ? row->displayName : "알 수 없는 물건";
	}

	std::string ItemNameEnchanted(uint16_t itemId, int enchant)
	{
		if (enchant > 0)
			return "+" + std::to_string(enchant) + " " + ItemName(itemId);
		return ItemName(itemId);
	}

	std::string RegionName(int zoneId)
	{
		const auto* r = WorldData::FindRegion(zoneId);
		return r ? r->name : "알 수 없는 곳";
	}

	std::string MonsterName(uint16_t species)
	{
		const MonsterTableRow* row = MonsterTable::Find(species);
		return row ? row->name : "몬스터";
	}

	std::string Number(long long v) { return util::FormatNumber(v); }

	std::string Stars(int tier)
	{
		std::string s;
		for (int i = 0; i < 5; ++i)
			s += (i < tier) ? "★" : "☆";
		return s;
	}

	std::string SystemMessage(SystemMessageCode code, int32_t p1, int32_t p2, bool& important)
	{
		important = false;
		switch (code)
		{
		case SystemMessageCode::InventoryFull:
			important = true;
			return "인벤토리가 가득 차서 " + Josa(ItemName(static_cast<uint16_t>(p1)), "을", "를") + " 얻지 못했습니다.";
		case SystemMessageCode::ExpLost:
			return "사망하여 경험치 " + Josa(Number(p1), "을", "를") + " 잃었습니다.";
		case SystemMessageCode::NotEnoughAdena:
			important = true;
			return "아데나가 부족합니다. (필요 " + Number(p1) + " / 보유 " + Number(p2) + ")";
		case SystemMessageCode::SafeZoneNoCombat:
			important = true;
			return "마을 안에서는 싸울 수 없습니다.";
		case SystemMessageCode::ItemDroppedOnDeath:
			important = true;
			return "카오틱 상태로 쓰러져 " + Josa(ItemNameEnchanted(static_cast<uint16_t>(p1), p2), "을", "를") + " 떨어뜨렸습니다!";
		case SystemMessageCode::BecamePurple:
			important = true;
			return Fmt("먼저 공격하여 보라색 이름이 되었습니다. %d초 동안은 반격을 받아도 상대에게 벌칙이 없습니다.", p1 / 1000);
		case SystemMessageCode::BecameChaotic:
			important = true;
			return "무고한 자를 쓰러뜨려 카오틱이 되었습니다. (성향치 " + Number(p1) + ")";
		case SystemMessageCode::HasteStart:
			return Fmt("몸이 가벼워졌습니다! (%d초)", p1);
		case SystemMessageCode::HasteEnd:
			return "가속 효과가 사라졌습니다.";
		case SystemMessageCode::ReturnedToTown:
			return JosaRo(std::string(WorldData::kTownName)) + " 귀환했습니다.";
		case SystemMessageCode::Teleported:
			return JosaRo(RegionName(p1)) + " 순간이동했습니다.";
		case SystemMessageCode::LevelUp:
			return Fmt("레벨이 올랐습니다! (Lv.%d)", p1);
		case SystemMessageCode::TargetInvalid:
			return "대상이 올바르지 않습니다.";
		case SystemMessageCode::TargetTooFar:
			return "대상이 너무 멀리 있습니다.";
		case SystemMessageCode::ItemCooldown:
			return Fmt("아직 사용할 수 없습니다. (%.1f초)", p1 / 1000.f);
		case SystemMessageCode::CannotUseHere:
			important = true;
			return "여기서는 사용할 수 없습니다.";
		case SystemMessageCode::AlignmentRecovered:
			important = true;
			return "성향치가 회복되었습니다. (성향치 " + Number(p1) + ")";
		}
		return "알 수 없는 알림";
	}

	std::string WorldNotice(WorldNoticeCode code, int32_t p1, int32_t p2, const std::string& name)
	{
		switch (code)
		{
		case WorldNoticeCode::BossSpawned:
			return RegionName(p2) + "에 " + Josa(MonsterName(static_cast<uint16_t>(p1)), "이", "가") + " 나타났습니다!";
		case WorldNoticeCode::BossKilled:
			return Josa(name, "이", "가") + " " + RegionName(p2) + "의 " + Josa(MonsterName(static_cast<uint16_t>(p1)), "을", "를") + " 쓰러뜨렸습니다!";
		case WorldNoticeCode::EnchantSuccess:
			return Josa(name, "이", "가") + " " + Josa(ItemNameEnchanted(static_cast<uint16_t>(p1), p2), "을", "를") + " 만들어 냈습니다!";
		case WorldNoticeCode::PlayerKilled:
			return RegionName(p1) + "에서 " + Josa(name, "이", "가") + " 무고한 자를 쓰러뜨렸습니다.";
		}
		return "";
	}

	std::string ShopResultText(ShopKind kind, ShopResult result)
	{
		switch (result)
		{
		case ShopResult::Success:
			return kind == ShopKind::Buy ? "구매했습니다." : (kind == ShopKind::Sell ? "판매했습니다." : "순간이동합니다.");
		case ShopResult::Duplicate:      return "이미 처리된 요청입니다.";
		case ShopResult::NotEnoughAdena: return "아데나가 부족합니다.";
		case ShopResult::InventoryFull:  return "인벤토리에 빈 칸이 없습니다.";
		case ShopResult::TooFar:         return "상인과 너무 멀리 떨어져 있습니다.";
		case ShopResult::InvalidItem:    return kind == ShopKind::Sell ? "그 물건은 팔 수 없습니다." : "그 물건은 여기서 팔지 않습니다.";
		case ShopResult::InvalidCount:   return "수량이 올바르지 않습니다.";
		case ShopResult::EquippedItem:   return "착용 중인 장비는 팔 수 없습니다.";
		case ShopResult::InvalidState:   return "지금은 할 수 없습니다.";
		case ShopResult::InvalidNpc:     return "상인을 찾을 수 없습니다.";
		}
		return "";
	}
}
