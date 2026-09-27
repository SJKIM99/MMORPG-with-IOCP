#pragma once

#include <cstdint>
#include <cstddef>

#include "Data/CombatData.h"
#include "Data/ItemRules.h"
#include "Item/ItemTableData.h"

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사 · 경매장 규칙 — 서버 · 클라 공용(검사 · 표시 · 검색 분류가 같은 값을 쓴다).
//
// 이름은 WORLD.md 6장: 결사(길드) · 결사 회관 · 경매장 · 정산함, 직위 결사장 · 부관 · 정예 · 결사원.
// 설계 · 스레드 소유권은 docs/adr/0001-guild-auction-services.md.
// ─────────────────────────────────────────────────────────────────────────────
namespace GuildRule
{
	inline constexpr uint8_t  kMinLevel     = 15;       // 세우는 사람의 레벨
	inline constexpr uint32_t kCreateCost   = 10000;    // 세우는 값(그로시)
	inline constexpr int      kMaxMembers   = 30;
	inline constexpr int      kNameMinChars = 2;        // 글자(UTF-8 코드 포인트) 수
	inline constexpr int      kNameMaxChars = 8;
	inline constexpr int      kNameBytes    = 25;       // 한글 8자(24B) + 널
	inline constexpr int      kNoticeBytes  = 91;       // 한글 30자(90B) + 널
	inline constexpr uint32_t kInviteMs     = 30000;    // 초대에 답할 시간
	inline constexpr int      kEmblemShapes = 12;       // 문장 모양(방패 · 종 · 별 · 늑대 · 매 · 참나무 · 탑 · 열쇠 · 칼 · 달 · 불꽃 · 물결)
	inline constexpr int      kEmblemColors = 6;        // 문장 바탕색
	inline constexpr int      kMembersPerPage = 8;      // 구성원 목록 한 패킷
	inline constexpr uint16_t kHallNpcId    = 22;       // 결사 회관지기 즈데노 — 세우기 · 문장은 그의 곁에서(WorldData kNpcs)
	inline constexpr int      kHallTop      = 3;        // 깃발 뜰 순위(GUILD_INF Top) — 깃대 둘(props.txt 결사 회관 단)에 1 · 2위, 3위는 회관지기의 말로
	inline constexpr uint32_t kTopQueryMs   = 2000;     // Top 요청 간격(사람마다, 소유 Zone이 거른다)

	enum class Rank : uint8_t { Leader = 0, Officer = 1, Elite = 2, Member = 3, Count };
	inline constexpr const char* kRankName[] = { "결사장", "부관", "정예", "결사원" };
	static_assert(sizeof(kRankName) / sizeof(kRankName[0]) == static_cast<size_t>(Rank::Count));

	[[nodiscard]] constexpr const char* RankName(uint8_t r) noexcept { return r < static_cast<uint8_t>(Rank::Count) ? kRankName[r] : ""; }

	// 권한표(PHASE_5.md 2장) — 숫자가 작을수록 높다
	[[nodiscard]] constexpr bool CanInvite(Rank r) noexcept { return r <= Rank::Elite; }
	[[nodiscard]] constexpr bool CanKick(Rank actor, Rank target) noexcept { return actor <= Rank::Officer && actor < target; }
	[[nodiscard]] constexpr bool CanNotice(Rank r) noexcept { return r <= Rank::Officer; }
	// 직위 바꾸기: 결사장은 결사장 아래 셋 사이를 자유롭게(결사장 넘기기는 따로), 부관은 자기보다 아래(정예 · 결사원) 사이만
	[[nodiscard]] constexpr bool CanSetRank(Rank actor, Rank target, Rank to) noexcept
	{
		if (to == Rank::Leader || target == Rank::Leader || to >= Rank::Count) return false;
		if (actor == Rank::Leader) return true;
		return actor == Rank::Officer && target > Rank::Officer && to > Rank::Officer;
	}

	[[nodiscard]] constexpr uint8_t Emblem(int shape, int color) noexcept
	{
		return static_cast<uint8_t>((shape % kEmblemShapes) * kEmblemColors + (color % kEmblemColors));
	}
	[[nodiscard]] constexpr int EmblemShape(uint8_t e) noexcept { return (e / kEmblemColors) % kEmblemShapes; }
	[[nodiscard]] constexpr int EmblemColor(uint8_t e) noexcept { return e % kEmblemColors; }
	[[nodiscard]] constexpr bool ValidEmblem(uint8_t e) noexcept { return e < kEmblemShapes * kEmblemColors; }

	// 금칙어(결사 이름) — 운영 사칭 · 욕설. 영문은 대소문자를 가리지 않고, 숫자를 끼워 넣어도(씨8발) 걸리게 숫자는 건너뛰고 본다.
	// 이름은 한글 음절 · 영문 · 숫자만 되므로(띄어쓰기 · 기호 없음) 바이트 부분 문자열로 충분하다(UTF-8 첫 바이트와 이음 바이트는 겹치지 않는다).
	inline constexpr const char* kBannedWords[] = {
		"운영자", "운영진", "관리자", "개발자", "공식", "시스템", "gm", "admin", "operator",
		"시발", "씨발", "씨바", "병신", "븅신", "개새", "새끼", "좆", "존나", "지랄", "엿먹", "니미", "느금",
		"fuck", "shit", "bitch",
	};
	[[nodiscard]] constexpr char LowerAscii(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
	[[nodiscard]] constexpr bool HasBannedWord(const char* s) noexcept
	{
		for (const char* w : kBannedWords)
			for (size_t i = 0; s[i] != '\0'; ++i)
			{
				size_t a = i, b = 0;
				while (w[b] != '\0' && s[a] != '\0')
				{
					if (s[a] >= '0' && s[a] <= '9' && a != i) { ++a; continue; }   // 사이에 끼운 숫자는 건너뛴다
					if (LowerAscii(s[a]) != w[b]) break;
					++a;
					++b;
				}
				if (w[b] == '\0') return true;
			}
		return false;
	}

	// 이름 검사(서버가 판정 — 클라는 입력 중 안내에 같은 함수를 쓴다). UTF-8을 읽어 글자 수 · 문자 종류를 본다.
	// 허용: 한글 음절(가~힣) · 영문 · 숫자. 띄어쓰기 · 기호 · 자모만 있는 글자 · 금칙어는 안 된다.
	enum class NameCheck : uint8_t { Ok, TooShort, TooLong, BadChar, Banned };   // 끝에만 추가
	[[nodiscard]] constexpr NameCheck CheckName(const char* s) noexcept
	{
		int chars = 0;
		for (size_t i = 0; s[i] != '\0';)
		{
			const auto c = static_cast<unsigned char>(s[i]);
			if (c < 0x80)
			{
				const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
				if (!ok) return NameCheck::BadChar;
				++i;
			}
			else if ((c & 0xF0) == 0xE0)
			{
				const auto c1 = static_cast<unsigned char>(s[i + 1]);
				if (c1 == 0) return NameCheck::BadChar;
				const auto c2 = static_cast<unsigned char>(s[i + 2]);
				if (c2 == 0) return NameCheck::BadChar;
				const uint32_t cp = ((c & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
				if (cp < 0xAC00 || cp > 0xD7A3) return NameCheck::BadChar;   // 한글 음절만
				i += 3;
			}
			else
				return NameCheck::BadChar;
			if (++chars > kNameMaxChars) return NameCheck::TooLong;
		}
		if (chars < kNameMinChars) return NameCheck::TooShort;
		return HasBannedWord(s) ? NameCheck::Banned : NameCheck::Ok;
	}
	static_assert(CheckName("새벽서약") == NameCheck::Ok && CheckName("Dawn7") == NameCheck::Ok && CheckName("시바견모임") == NameCheck::Ok);
	static_assert(CheckName("운영자모임") == NameCheck::Banned && CheckName("GM결사") == NameCheck::Banned && CheckName("씨8발") == NameCheck::Banned);
}

namespace AuctionRule
{
	inline constexpr int      kMaxListingsPerSeller = 10;
	inline constexpr uint8_t  kDurationHours[] = { 12, 24, 48 };
	inline constexpr int      kDurationCount = 3;
	inline constexpr uint32_t kMinPrice = 10;
	inline constexpr uint32_t kMaxPrice = 100000000;
	inline constexpr int      kPageRows = 6;            // 검색 결과 · 내 경매 · 정산함 한 패킷
	inline constexpr int      kQueryBytes = 25;         // 이름 검색(한글 8자)
	inline constexpr uint32_t kExpireTickMs = 60000;    // 만료 확인(1분)
	inline constexpr int      kPriceHistory = 20;       // 품목마다 기억하는 최근 거래가
	inline constexpr uint32_t kHourSecDefault = 3600;   // 기간 한 시간의 실제 초(시험: 환경 변수 HARVEN_AUCTION_HOUR_SEC)
	inline constexpr uint16_t kAuctioneerNpcId = 21;    // 경매인 로렌초 — 대화창의 경매장 · 정산함 단추(거래 자체는 마을 안 어디서나)

	// 보증금 1%(돌려주지 않는다) · 판매 수수료 5% — 둘 다 최소 1
	[[nodiscard]] constexpr uint32_t Deposit(uint32_t price) noexcept { const uint32_t d = price / 100; return d < 1 ? 1 : d; }
	[[nodiscard]] constexpr uint32_t Fee(uint32_t price) noexcept { const uint32_t f = price / 20; return f < 1 ? 1 : f; }
	[[nodiscard]] constexpr uint32_t SellerGets(uint32_t price) noexcept { return price - Fee(price); }
	[[nodiscard]] constexpr bool ValidPrice(uint32_t price) noexcept { return price >= kMinPrice && price <= kMaxPrice; }

	// 분류 트리(경매장 창 왼쪽) — 서비스의 검색 색인과 같은 함수를 쓴다
	enum class Cat : uint8_t { All, Weapon, Armor, Accessory, Consumable, Material, Etc, Count };
	enum class Sub : uint8_t
	{
		All,
		Knight, Engraver, NightHunter,                 // 무기 → 직업
		Helmet, Body, Gloves, Boots, Cloak,            // 방어구 → 부위
		Necklace, Ring,                                // 장신구
		Potion, Scroll,                                // 소모품
		Count
	};
	inline constexpr const char* kCatName[] = { "전체", "무기", "방어구", "장신구", "소모품", "재료", "기타" };
	inline constexpr const char* kSubName[] = { "전체", "서약기사", "각인술사", "밤사냥꾼", "투구", "갑옷", "장갑", "신발", "망토", "목걸이", "반지", "물약", "두루마리" };
	static_assert(sizeof(kCatName) / sizeof(kCatName[0]) == static_cast<size_t>(Cat::Count));
	static_assert(sizeof(kSubName) / sizeof(kSubName[0]) == static_cast<size_t>(Sub::Count));

	[[nodiscard]] constexpr EquipSlot SlotOfRow(const ItemTableRow& r) noexcept
	{
		return r.type == ItemType::eEquipment && r.equipSlot == EquipSlot::None ? EquipSlot::Weapon : r.equipSlot;
	}
	[[nodiscard]] constexpr Cat CategoryOf(const ItemTableRow& r) noexcept
	{
		switch (r.type)
		{
		case ItemType::eEquipment:
			switch (SlotOfRow(r))
			{
			case EquipSlot::Weapon: return Cat::Weapon;
			case EquipSlot::Necklace: case EquipSlot::Ring: return Cat::Accessory;
			default: return Cat::Armor;
			}
		case ItemType::eConsumable: return Cat::Consumable;
		case ItemType::eMaterial: return Cat::Material;
		default: return Cat::Etc;
		}
	}
	[[nodiscard]] constexpr Sub SubOf(const ItemTableRow& r) noexcept
	{
		if (r.type == ItemType::eConsumable)
			return r.effect == ItemEffect::eHeal || r.effect == ItemEffect::eHaste ? Sub::Potion : Sub::Scroll;
		if (r.type != ItemType::eEquipment) return Sub::All;
		switch (SlotOfRow(r))
		{
		case EquipSlot::Weapon:
		{
			// 직업 전용이면 그 직업, 아니면 무기 종류로(지팡이 = 각인술사, 단검 = 밤사냥꾼, 검 · 대검 · 곡도 = 서약기사)
			if (r.classMask & (1u << static_cast<unsigned>(ClassId::Engraver))) return Sub::Engraver;
			if (r.classMask & (1u << static_cast<unsigned>(ClassId::NightHunter))) return Sub::NightHunter;
			if (r.classMask & (1u << static_cast<unsigned>(ClassId::Knight))) return Sub::Knight;
			if (r.weaponKind == WeaponKind::Staff) return Sub::Engraver;
			if (r.weaponKind == WeaponKind::Dagger) return Sub::NightHunter;
			return Sub::Knight;
		}
		case EquipSlot::Helmet: return Sub::Helmet;
		case EquipSlot::Armor: return Sub::Body;
		case EquipSlot::Gloves: return Sub::Gloves;
		case EquipSlot::Boots: return Sub::Boots;
		case EquipSlot::Cloak: return Sub::Cloak;
		case EquipSlot::Necklace: return Sub::Necklace;
		case EquipSlot::Ring: return Sub::Ring;
		default: return Sub::All;
		}
	}
	// 올릴 수 있는 것: 그로시(지갑)가 아닌 표의 물건. 착용 중인 장비는 요청 단계에서 따로 막는다
	[[nodiscard]] constexpr bool Listable(const ItemTableRow& r) noexcept { return r.id != ITEM_TABLE_ID_NONE && r.type != ItemType::eCurrency; }

	enum class Sort : uint8_t { PriceAsc, PriceDesc, TimeLeft, RarityDesc, Count };
}
