#pragma once

#include "Data/CombatData.h"
#include "Data/SkillData.h"

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 기술 성장(Phase 2-D) — 기술 레벨 · 기술 점수 · 갈래. 서버 · 클라 공용.
//
//  · 기술 레벨 1~10(QWERASDF 여덟 칸의 기술만). 캐릭터 레벨이 오를 때마다 기술 점수 2점.
//    레벨을 올리는 값: 1→4 한 단계 1점 · 4→7 2점 · 7→10 3점(10까지 18점). 레벨마다 그 기술 피해 +5%(DealHit).
//    쓴 점수는 따로 저장하지 않는다 — 기술 레벨들에서 다시 계산한다(점수 ↔ 레벨이 어긋날 수 없다).
//  · 갈래(레벨 4 · 7에서 둘 중 하나): 로스트아크 트라이포드의 축소판. 갈래 하나 = 변형 한 줄(Mod + 값).
//    ApplyDef · ApplyRow가 기술 표(SkillDef · SkillRow)의 "복사본"을 바꾼다 — 서버 판정 · 클라 예측 · 연출이 같은 복사본을 쓴다.
//    표로 나타낼 수 없는 것(튕김 수 · 함성 시간 · 안개 틱 · 남는 장판 · 반격)은 서버가 FindMod로 읽어 처리한다.
//  · 레벨 · 갈래는 서버 권위(DB PlayerSkills), 어느 키에 둘지는 클라 설정.
//  · 주변 사람에게는 SKILL_EX_NFY.flags의 비트 1~4로 켜진 갈래만 알린다(레벨은 연출과 상관없다).
// ─────────────────────────────────────────────────────────────────────────────
namespace SkillGrowth
{
	inline constexpr uint8_t  kMaxLevel = 10;
	inline constexpr uint8_t  kTierLevel[2] = { 4, 7 };
	inline constexpr uint16_t kIdSpan = 40;            // 기술 id 상한(서버 · 클라 배열 크기)
	inline constexpr uint32_t kDamagePerLevel = 5;     // 레벨마다 피해 +%
	inline constexpr int      kSlots = 8;              // 자라는 칸: Q W E R A S D F

	// 레벨 from → from+1 값
	[[nodiscard]] constexpr uint8_t StepCost(uint8_t from) noexcept { return from < 4 ? 1 : (from < 7 ? 2 : 3); }
	// 레벨 1 → level 까지 쓴 점수
	[[nodiscard]] constexpr uint16_t CostTo(uint8_t level) noexcept
	{
		uint16_t c = 0;
		for (uint8_t l = 1; l < level && l < kMaxLevel; ++l) c = static_cast<uint16_t>(c + StepCost(l));
		return c;
	}
	// 캐릭터 레벨의 기술 점수(레벨 1 = 0점)
	[[nodiscard]] constexpr uint16_t PointsAt(uint8_t charLevel) noexcept { return charLevel > 1 ? static_cast<uint16_t>(2u * (charLevel - 1u)) : 0; }
	[[nodiscard]] constexpr uint32_t DamagePercent(uint8_t level) noexcept { return 100u + kDamagePerLevel * (level > 1 ? level - 1u : 0u); }

	// 기술 하나의 상태 — 한 바이트: 레벨-1(4비트) · 갈래 Ⅰ(2비트) · 갈래 Ⅱ(2비트). 0 = 레벨 1 · 갈래 없음
	struct State
	{
		uint8_t level = 1;
		uint8_t pick[2] = { 0, 0 };   // 0 = 고르지 않음, 1 · 2
	};
	[[nodiscard]] constexpr State Unpack(uint8_t b) noexcept
	{
		State s;
		s.level = static_cast<uint8_t>((b & 0x0F) + 1);
		if (s.level > kMaxLevel) s.level = kMaxLevel;
		s.pick[0] = static_cast<uint8_t>((b >> 4) & 3);
		s.pick[1] = static_cast<uint8_t>((b >> 6) & 3);
		if (s.pick[0] > 2) s.pick[0] = 0;
		if (s.pick[1] > 2) s.pick[1] = 0;
		return s;
	}
	[[nodiscard]] constexpr uint8_t Pack(const State& s) noexcept
	{
		const uint8_t lv = s.level < 1 ? 1 : (s.level > kMaxLevel ? kMaxLevel : s.level);
		return static_cast<uint8_t>(((lv - 1) & 0x0F) | ((s.pick[0] & 3) << 4) | ((s.pick[1] & 3) << 6));
	}
	// 켜진 갈래(레벨이 모자라면 골라 둔 것도 꺼진다)
	[[nodiscard]] constexpr uint8_t Active(const State& s, int tier) noexcept { return s.level >= kTierLevel[tier] ? s.pick[tier] : 0; }
	// SKILL_EX_NFY.flags 비트 1~2 = 갈래 Ⅰ, 3~4 = 갈래 Ⅱ(SkillExFlag)
	[[nodiscard]] constexpr uint8_t NfyBits(const State& s) noexcept { return static_cast<uint8_t>((Active(s, 0) << 1) | (Active(s, 1) << 3)); }
	[[nodiscard]] constexpr State FromNfy(uint8_t flags) noexcept
	{
		State s;
		s.level = kMaxLevel;
		s.pick[0] = static_cast<uint8_t>((flags >> 1) & 3);
		s.pick[1] = static_cast<uint8_t>((flags >> 3) & 3);
		if (s.pick[0] > 2) s.pick[0] = 0;
		if (s.pick[1] > 2) s.pick[1] = 0;
		return s;
	}

	// ── 변형 ────────────────────────────────────────────────────────────────
	enum class Mod : uint8_t
	{
		None,
		Wider,    // 범위 +value칸(원 · 고리 · 부채꼴 반경, 직선은 길이 +value · 폭 +2·value) — 예전 판정은 SkillRow.radius
		Echo,     // 주 타격이 value×10ms 뒤 한 번 더(피해 · 무력화 50%) — 예전 판정 기술은 서버가 Execute를 한 번 더
		Heavy,    // 피해 +value% · 무력화 +50% · 주 타격이 강타 · 재사용 대기 +15%
		Swift,    // 재사용 대기 −value% · 동작(준비 · 타격 시각 · 후딜) −25% · 모으기 단계 · 홀딩 틱 −20%
		Twin,     // 직선: ±value° 두 줄기 더(50%) · 부채꼴: value°만큼 넓게
		Pull,     // 맞은 적을 모양 가운데(시전자 기준 모양이면 시전자)로 끌어당긴다
		Element,  // 모든 타격이 상태이상 status를 건다(원래 상태이상 대신) — 연출 색 color
		Lasting,  // 주 타격 자리에 value초 동안 장판(0.5초마다 20% · status)
		Longer,   // 지속 +value%(홀딩 · 켜고 끄기 최대 · 상태이상 · 함성 · 안개 · 표식)
		Extra,    // 더 많이: 튕김 · 전도 +value · 상태이상 중첩 +value · 따라 하기 +value
		Steady,   // 동작 내내 경직 면역
		Count
	};

	struct Variant
	{
		Mod         mod = Mod::None;
		uint8_t     value = 0;
		StatusId    status = StatusId::None;   // Element · Lasting
		uint32_t    color = 0;                 // 연출 색(0 = 기술 색)
		const char* name = "";
		const char* desc = "";
	};
	struct Tree
	{
		uint16_t skillId = 0;
		Variant  v[2][2] = {};                 // [갈래 Ⅰ · Ⅱ][첫째 · 둘째]
	};

	namespace Detail
	{
		constexpr Variant V(Mod m, uint8_t value, const char* name, const char* desc, StatusId st = StatusId::None, uint32_t color = 0)
		{
			Variant v{};
			v.mod = m; v.value = value; v.name = name; v.desc = desc; v.status = st; v.color = color;
			return v;
		}
		constexpr Tree T(uint16_t id, Variant a1, Variant a2, Variant b1, Variant b2)
		{
			Tree t{};
			t.skillId = id;
			t.v[0][0] = a1; t.v[0][1] = a2; t.v[1][0] = b1; t.v[1][1] = b2;
			return t;
		}
		using M = Mod;
		using S = StatusId;
		// 자주 쓰는 갈래(이름 · 설명이 같다)
		constexpr Variant kWider = V(M::Wider, 1, "넓은 범위", "범위가 1칸 넓어진다.");
		constexpr Variant kHeavy = V(M::Heavy, 30, "묵직함", "피해 +30% · 무력화 +50%. 재사용 대기 +15%.");
		constexpr Variant kSwift = V(M::Swift, 25, "재빠름", "재사용 대기 −25%. 동작이 빨라진다.");
		constexpr Variant kPull = V(M::Pull, 2, "끌어당김", "맞은 적을 가운데로 끌어당긴다.");
		constexpr Variant kSteady = V(M::Steady, 0, "굳건함", "동작 내내 경직 면역.");
	}

	// 기술마다 갈래 넷(이름 · 설명은 WORLD.md 용어집 「갈래」). 순서 = 레벨 4 [첫째, 둘째] · 레벨 7 [첫째, 둘째]
	inline constexpr Tree kTrees[] =
	{
		// ── 서약기사 ────────────────────────────────────────────────────────────
		Detail::T(2,  Detail::kHeavy, Detail::kSwift,
			Detail::V(Mod::Echo, 35, "두 번 울림", "0.35초 뒤 방패가 한 번 더 울린다(50%)."),
			Detail::V(Mod::Longer, 60, "긴 울림", "기절이 60% 길어진다.")),
		Detail::T(3,  Detail::kWider, Detail::V(Mod::Pull, 2, "끌어당김", "도는 동안 주변 적을 끌어당긴다."),
			Detail::kHeavy,
			Detail::V(Mod::Element, 0, "불씨 회오리", "베일 때마다 화상을 건다.", StatusId::Burn, 0xFF8A3A)),
		Detail::T(19, Detail::V(Mod::Swift, 25, "재빠름", "모으기 단계가 빨라진다. 재사용 대기 −25%."), Detail::kWider,
			Detail::V(Mod::Echo, 35, "두 번 터짐", "마지막 균열이 0.35초 뒤 한 번 더 터진다(50%)."),
			Detail::V(Mod::Lasting, 3, "빛 균열", "균열 끝이 3초 동안 빛나며 밟은 적을 벤다.", StatusId::None, 0xFFE9A8)),
		Detail::T(20, Detail::kHeavy, Detail::kSwift,
			Detail::V(Mod::Extra, 2, "다섯 번 튕김", "방패가 적 사이를 다섯 번 튕긴다."),
			Detail::V(Mod::Element, 0, "종 울림", "맞은 적을 잠깐 기절시킨다.", StatusId::Stun, 0xFFE08A)),
		Detail::T(4,  Detail::kSwift, Detail::kSteady,
			Detail::kHeavy,
			Detail::V(Mod::Lasting, 3, "불씨 자국", "멈춘 자리에 3초 동안 불씨가 남는다.", StatusId::Burn, 0xFF9A4A)),
		Detail::T(5,  Detail::V(Mod::Longer, 50, "오래 버팀", "최대 6초까지 켜 둘 수 있다."),
			Detail::V(Mod::Heavy, 30, "날 선 반격", "반격 피해 +30%."),
			Detail::V(Mod::Element, 0, "불꽃 반격", "반격이 화상을 건다.", StatusId::Burn, 0xFF8A3A),
			Detail::V(Mod::Element, 0, "울림 반격", "반격이 적을 잠깐 기절시킨다.", StatusId::Stun, 0xFFE08A)),
		Detail::T(21, Detail::kWider, Detail::kSwift,
			Detail::V(Mod::Longer, 50, "긴 함성", "함성(공격 +15%)이 12초 간다."),
			Detail::V(Mod::Pull, 2, "끌어당김", "함성이 주변 적을 끌어당긴다.")),
		Detail::T(22, Detail::V(Mod::Swift, 25, "재빠름", "준비가 짧아진다. 재사용 대기 −25%."), Detail::kWider,
			Detail::V(Mod::Pull, 2, "끌어당김", "울릴 때마다 적을 종 아래로 끌어당긴다."),
			Detail::V(Mod::Echo, 40, "넷째 울림", "마지막 울림 뒤 한 번 더 울린다(50%).")),

		// ── 각인술사 ────────────────────────────────────────────────────────────
		Detail::T(8,  Detail::kWider, Detail::kSwift,
			Detail::V(Mod::Lasting, 3, "서리 들판", "3초 동안 서리가 남아 둔화를 건다.", StatusId::Slow, 0xBFE6FF),
			Detail::V(Mod::Echo, 35, "두 번 솟음", "0.35초 뒤 얼음이 한 번 더 솟는다(50%).")),
		Detail::T(9,  Detail::V(Mod::Echo, 35, "두 번 퍼짐", "불꽃 고리가 0.35초 뒤 한 번 더 퍼진다(50%)."), Detail::kWider,
			Detail::V(Mod::Element, 0, "푸른 불꽃", "화상 대신 둔화를 건다 — 푸르게 타는 불꽃.", StatusId::Slow, 0x7FB8FF),
			Detail::V(Mod::Lasting, 3, "남은 불길", "3초 동안 불길이 남는다.", StatusId::Burn, 0xFF8A3A)),
		Detail::T(11, Detail::V(Mod::Extra, 2, "일곱 번 튐", "벼락이 두 번 더 튄다."), Detail::kSwift,
			Detail::kHeavy,
			Detail::V(Mod::Element, 0, "마비 벼락", "표식 대신 짧은 기절을 건다.", StatusId::Stun, 0xE8F6FF)),
		Detail::T(25, Detail::V(Mod::Swift, 25, "재빠름", "모으기 단계가 빨라진다. 재사용 대기 −25%."), Detail::kHeavy,
			Detail::V(Mod::Twin, 25, "세 줄기", "창이 세 줄기로 나간다(옆 줄기 50%)."),
			Detail::V(Mod::Lasting, 3, "서리 자취", "창이 멈춘 자리에 3초 동안 서리가 남는다.", StatusId::Slow, 0xBFE6FF)),
		Detail::T(26, Detail::V(Mod::Swift, 25, "재빠름", "준비가 짧아진다. 재사용 대기 −25%."), Detail::kWider,
			Detail::V(Mod::Pull, 2, "끌어당김", "불기둥이 적을 가운데로 끌어당긴다."),
			Detail::kHeavy),
		Detail::T(27, Detail::kWider, Detail::V(Mod::Longer, 50, "긴 결계", "둔화 · 표식이 50% 오래 간다."),
			Detail::V(Mod::Pull, 2, "조이는 결계", "결계가 안쪽으로 조여 적을 끌어당긴다."),
			Detail::V(Mod::Echo, 50, "두 겹 결계", "0.5초 뒤 한 번 더 조인다(50%).")),
		Detail::T(28, Detail::V(Mod::Swift, 25, "재빠름", "0.9초 만에 떨어진다. 재사용 대기 −25%."), Detail::kWider,
			Detail::V(Mod::Echo, 40, "두 번 낙뢰", "0.4초 뒤 한 번 더 떨어진다(50%)."),
			Detail::kHeavy),
		Detail::T(29, Detail::kWider, Detail::V(Mod::Swift, 25, "재빠름", "얼음 파편이 더 빠르게 몰아친다. 재사용 대기 −25%."),
			Detail::V(Mod::Longer, 50, "긴 눈보라", "3초까지 뿜는다."),
			Detail::V(Mod::Pull, 2, "빨아들임", "눈보라가 적을 앞으로 빨아들인다.")),

		// ── 밤사냥꾼 ────────────────────────────────────────────────────────────
		Detail::T(15, Detail::kWider, Detail::kSwift,
			Detail::V(Mod::Echo, 30, "두 번 베기", "0.3초 뒤 한 번 더 벤다(50%)."),
			Detail::V(Mod::Extra, 1, "진한 독", "중독이 두 번씩 쌓인다.")),
		Detail::T(14, Detail::kSwift, Detail::kSteady,
			Detail::V(Mod::Lasting, 3, "먹물 웅덩이", "도착한 자리에 3초 동안 먹물이 번져 둔화를 건다.", StatusId::Slow, 0x5A6272),
			Detail::kHeavy),
		Detail::T(17, Detail::kHeavy, Detail::kSwift,
			Detail::V(Mod::Echo, 30, "두 번 찌르기", "0.3초 뒤 한 번 더 찌른다(50%)."),
			Detail::V(Mod::Element, 0, "독 칼끝", "찌를 때 중독을 건다.", StatusId::Poison, 0x8AE85A)),
		Detail::T(31, Detail::kWider, Detail::V(Mod::Pull, 2, "끌어당김", "도는 칼날이 주변 적을 끌어당긴다."),
			Detail::V(Mod::Longer, 50, "긴 폭풍", "2.4초까지 돈다."),
			Detail::V(Mod::Element, 0, "독 폭풍", "베일 때마다 중독을 건다.", StatusId::Poison, 0x8AE85A)),
		Detail::T(32, Detail::V(Mod::Extra, 2, "다섯 번 따라 함", "분신이 다섯 번 따라 한다."), Detail::kSwift,
			Detail::V(Mod::Longer, 50, "긴 분신", "분신이 50% 오래 남는다."),
			Detail::V(Mod::Heavy, 50, "짙은 분신", "분신이 따라 하는 피해 40% → 60%.")),
		Detail::T(33, Detail::kWider, Detail::kSwift,
			Detail::V(Mod::Twin, 20, "세 줄기", "부채꼴이 넓어지고, 셋째 투척이 세 줄기로 나간다."),
			Detail::V(Mod::Element, 0, "독 비수", "비수가 중독을 건다.", StatusId::Poison, 0x8AE85A)),
		Detail::T(34, Detail::kWider, Detail::V(Mod::Longer, 50, "긴 안개", "안개가 6초 동안 남는다."),
			Detail::V(Mod::Pull, 2, "빨아들이는 안개", "안개가 적을 가운데로 빨아들인다."),
			Detail::V(Mod::Extra, 1, "짙은 안개", "중독이 두 번씩 쌓인다.")),
		Detail::T(35, Detail::V(Mod::Swift, 25, "재빠름", "모으기가 빨라진다. 재사용 대기 −25%."), Detail::kWider,
			Detail::V(Mod::Echo, 30, "두 번 일섬", "베인 선이 0.3초 뒤 한 번 더 터진다(50%)."),
			Detail::kHeavy),
	};
	inline constexpr int kTreeCount = static_cast<int>(sizeof(kTrees) / sizeof(kTrees[0]));

	[[nodiscard]] constexpr const Tree* FindTree(uint16_t id) noexcept
	{
		for (int i = 0; i < kTreeCount; ++i)
			if (kTrees[i].skillId == id) return &kTrees[i];
		return nullptr;
	}
	[[nodiscard]] constexpr bool Growable(uint16_t id) noexcept { return FindTree(id) != nullptr; }
	// 켜진 갈래(없으면 nullptr)
	[[nodiscard]] constexpr const Variant* ActiveVariant(uint16_t id, const State& s, int tier) noexcept
	{
		const Tree* t = FindTree(id);
		const uint8_t p = Active(s, tier);
		return (t && p >= 1 && p <= 2) ? &t->v[tier][p - 1] : nullptr;
	}
	// 켜진 갈래 중 이 변형(없으면 nullptr)
	[[nodiscard]] constexpr const Variant* FindMod(uint16_t id, const State& s, Mod m) noexcept
	{
		for (int tier = 0; tier < 2; ++tier)
			if (const Variant* v = ActiveVariant(id, s, tier); v && v->mod == m) return v;
		return nullptr;
	}
	[[nodiscard]] constexpr uint32_t Scale(uint32_t v, uint32_t pct) noexcept { return v * pct / 100u; }
	[[nodiscard]] constexpr uint16_t Scale16(uint32_t v, uint32_t pct) noexcept { const uint32_t r = v * pct / 100u; return static_cast<uint16_t>(r > 65535u ? 65535u : r); }
	[[nodiscard]] constexpr uint8_t Clamp8(uint32_t v) noexcept { return static_cast<uint8_t>(v > 255u ? 255u : v); }

	// 주 타격(메아리 · 강타 · 장판이 따르는 것): 틱 · 완벽 구간 전용이 아닌 마지막 타격, 없으면 마지막
	[[nodiscard]] constexpr int MainHit(const SkillDef& d) noexcept
	{
		for (int i = d.hitCount - 1; i >= 0; --i)
			if ((d.hits[i].flags & (HF_Tick | HF_Perfect)) == 0 && d.hits[i].tag == HT_None) return i;
		return d.hitCount - 1;
	}

	// 상태이상을 새로 걸 때 기본 시간(ms)
	[[nodiscard]] constexpr uint16_t DefaultStatusMs(StatusId st) noexcept
	{
		switch (st)
		{
		case StatusId::Stun: return 600;
		case StatusId::Freeze: return 1500;
		case StatusId::Burn: return 3000;
		case StatusId::Poison: return 5000;
		case StatusId::Slow: return 2500;
		case StatusId::Marked: return 5000;
		default: return 2000;
		}
	}

	// 기술 표(SkillDef) 복사본에 켜진 갈래를 입힌다 — 서버 판정 · 클라 예측 · 연출 공용
	[[nodiscard]] constexpr SkillDef ApplyDef(const SkillDef& base, const SkillRow& row, const State& s) noexcept
	{
		SkillDef d = base;
		for (int tier = 0; tier < 2; ++tier)
		{
			const Variant* vp = ActiveVariant(base.id, s, tier);
			if (!vp) continue;
			const Variant& v = *vp;
			const uint32_t baseCd = d.cooldownMs > 0 ? d.cooldownMs : row.cooldownMs;
			switch (v.mod)
			{
			case Mod::Wider:
				for (int i = 0; i < d.hitCount; ++i)
				{
					SkillHit& h = d.hits[i];
					if (h.shape == HitShape::Target) continue;
					h.a = Clamp8(h.a + v.value);
					if (h.shape == HitShape::Line) h.b = Clamp8(h.b + 2u * v.value);
				}
				break;
			case Mod::Echo:
				// 예전 판정 기술(Legacy)은 서버가 Execute를 한 번 더 부른다(표의 타격은 시각 표시용 하나뿐)
				if (d.special != SkillSpecial::Legacy && d.hitCount > 0 && d.hitCount < 8)
				{
					const int m = MainHit(d);
					SkillHit e = d.hits[m];
					e.atMs = static_cast<uint16_t>(e.atMs + v.value * 10u);
					e.coeff = static_cast<uint16_t>(e.coeff / 2u);
					e.stagger = static_cast<uint8_t>(e.stagger / 2u);
					e.flags = static_cast<uint8_t>(e.flags & ~(HF_Counter | HF_Final));
					e.tag = HT_Echo;
					e.src = static_cast<uint8_t>(m);
					d.hits[d.hitCount++] = e;
				}
				break;
			case Mod::Heavy:
				for (int i = 0; i < d.hitCount; ++i)
				{
					d.hits[i].coeff = Scale16(d.hits[i].coeff, 100u + v.value);
					d.hits[i].stagger = Clamp8(d.hits[i].stagger * 3u / 2u);
				}
				if (d.hitCount > 0) d.hits[MainHit(d)].flags = static_cast<uint8_t>(d.hits[MainHit(d)].flags | HF_Heavy);
				d.cooldownMs = Scale(baseCd, 115);
				break;
			case Mod::Swift:
				d.cooldownMs = Scale(baseCd, 100u - v.value);
				d.lockMs = Scale16(d.lockMs, 75);
				d.castMs = Scale16(d.castMs, 75);
				for (int i = 0; i < d.hitCount; ++i) d.hits[i].atMs = Scale16(d.hits[i].atMs, 75);
				if (d.kind == SkillKind::Charge) { d.stageMs = Scale16(d.stageMs, 80); }
				if (d.kind == SkillKind::Hold) { d.tickMs = Scale16(d.tickMs, 80); }
				break;
			case Mod::Twin:
			{
				const int n = d.hitCount;
				for (int i = 0; i < n; ++i)
				{
					SkillHit& h = d.hits[i];
					if (h.flags & HF_Tick) continue;
					if (h.shape == HitShape::Cone)
						h.b = Clamp8(h.b + v.value > 90u ? 90u : h.b + v.value);
					else if (h.shape == HitShape::Line && d.hitCount + 2 <= 8)
					{
						for (int k = 0; k < 2; ++k)
						{
							SkillHit t = h;
							t.turn = static_cast<int8_t>(k == 0 ? -static_cast<int>(v.value) : static_cast<int>(v.value));
							t.coeff = static_cast<uint16_t>(t.coeff / 2u);
							t.stagger = static_cast<uint8_t>(t.stagger / 2u);
							t.flags = static_cast<uint8_t>(t.flags & ~(HF_Final | HF_Counter));
							t.tag = HT_Twin;
							t.src = static_cast<uint8_t>(i);
							d.hits[d.hitCount++] = t;
						}
					}
				}
				break;
			}
			case Mod::Pull:
				for (int i = 0; i < d.hitCount; ++i) d.hits[i].react = HitReact::Pull;
				break;
			case Mod::Element:
				for (int i = 0; i < d.hitCount; ++i) d.hits[i].flags = static_cast<uint8_t>(d.hits[i].flags | HF_Status);
				break;
			case Mod::Longer:
				if (d.kind == SkillKind::Hold || d.kind == SkillKind::Toggle)
				{
					const uint16_t more = static_cast<uint16_t>(Scale16(d.holdMaxMs, 100u + v.value) - d.holdMaxMs);
					d.holdMaxMs = static_cast<uint16_t>(d.holdMaxMs + more);
					if (d.superArmorMs > 0) d.superArmorMs = static_cast<uint16_t>(d.superArmorMs + more);
				}
				break;
			case Mod::Steady:
			{
				uint32_t last = 0;
				for (int i = 0; i < d.hitCount; ++i) if (d.hits[i].atMs > last) last = d.hits[i].atMs;
				const uint32_t armor = d.castMs + d.lockMs + last + 300u;
				if (d.superArmorMs < armor) d.superArmorMs = static_cast<uint16_t>(armor > 65535u ? 65535u : armor);
				break;
			}
			case Mod::Lasting:   // 서버 SkillEx(주 타격 뒤 장판) · 클라 연출
			case Mod::Extra:     // ApplyRow(중첩 · 전도) + 서버 SkillEx(튕김)
			case Mod::None:
			case Mod::Count:
				break;
			}
		}
		return d;
	}

	// 기술 행(SkillRow) 복사본 — 예전 판정(Execute) · DealHit(상태이상)이 읽는 값
	[[nodiscard]] constexpr SkillRow ApplyRow(const SkillRow& base, const State& s) noexcept
	{
		SkillRow r = base;
		for (int tier = 0; tier < 2; ++tier)
		{
			const Variant* vp = ActiveVariant(base.id, s, tier);
			if (!vp) continue;
			const Variant& v = *vp;
			switch (v.mod)
			{
			case Mod::Wider: r.radius = Clamp8(r.radius + v.value); break;
			case Mod::Heavy: r.coeff = Scale16(r.coeff, 100u + v.value); break;
			case Mod::Element:
				r.status = v.status;
				if (r.statusMs == 0 || base.status != v.status) r.statusMs = DefaultStatusMs(v.status);
				if (r.statusStacks == 0) r.statusStacks = 1;
				break;
			case Mod::Longer: r.statusMs = Scale16(r.statusMs, 100u + v.value); break;
			case Mod::Extra:
				if (r.status != StatusId::None) r.statusStacks = Clamp8(r.statusStacks + v.value);
				r.hits = Clamp8(r.hits + v.value);
				break;
			default: break;
			}
		}
		return r;
	}

	// 설명 한 줄(툴팁 · 기술 창) — 이름 · 설명은 표에 있다
	[[nodiscard]] constexpr const char* TierName(int tier) noexcept { return tier == 0 ? "갈래 Ⅰ" : "갈래 Ⅱ"; }

	namespace Detail
	{
		constexpr bool Validate() noexcept
		{
			for (int i = 0; i < kTreeCount; ++i)
			{
				const Tree& t = kTrees[i];
				if (t.skillId >= kIdSpan) return false;
				const SkillDef* d = SkillDefTable::Find(t.skillId);
				const SkillRow* r = SkillTable::Find(t.skillId);
				if (!d || !r) return false;
				// 자라는 칸은 QWERASDF뿐
				if (d->key == SkillKey::C || d->key == SkillKey::V || d->key == SkillKey::Space || d->key == SkillKey::Z) return false;
				for (int tier = 0; tier < 2; ++tier)
					for (int k = 0; k < 2; ++k)
					{
						const Variant& v = t.v[tier][k];
						if (v.mod == Mod::None || v.name == nullptr || v.name[0] == 0 || v.desc == nullptr || v.desc[0] == 0) return false;
						if (v.mod == Mod::Element && v.status == StatusId::None) return false;
						// 같은 단의 두 갈래는 달라야 한다
						if (k == 1 && t.v[tier][0].mod == v.mod && t.v[tier][0].status == v.status) return false;
					}
				// 모든 조합에서 타격 수가 8 안(메아리 · 세 줄기)
				for (uint8_t a = 0; a <= 2; ++a)
					for (uint8_t b = 0; b <= 2; ++b)
					{
						State s; s.level = kMaxLevel; s.pick[0] = a; s.pick[1] = b;
						const SkillDef e = ApplyDef(*d, *r, s);
						if (e.hitCount > 8) return false;
					}
				for (int j = i + 1; j < kTreeCount; ++j)
					if (kTrees[j].skillId == t.skillId) return false;
			}
			// 세 직업 × 여덟 칸이 모두 갈래를 가진다
			for (int c = 0; c < 3; ++c)
				for (int k = static_cast<int>(SkillKey::Q); k <= static_cast<int>(SkillKey::F); ++k)
					if (FindTree(SkillDefTable::Default(static_cast<ClassId>(c), static_cast<SkillKey>(k))) == nullptr) return false;
			if (CostTo(kMaxLevel) != 18) return false;
			return true;
		}
	}
	static_assert(Detail::Validate(), "SkillGrowth: 갈래 표가 기술 표와 맞지 않습니다(칸 · 이름 · 타격 수).");
	static_assert(sizeof(State) == 3, "State는 3바이트(레벨 · 갈래 둘)");
}
