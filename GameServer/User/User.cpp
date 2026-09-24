#include "pch.h"
#include "Data/MoveRule.h"
#include "User.h"
#include "TimerThread.h"

void User::InitInstance()
{
	Subject::InitInstance();

	GetStat()->SetMaxHp(PLAYER_MAX_HP);
	GetStat()->SetHp(PLAYER_MAX_HP);
	GetStat()->SetOffensive(PLAYER_OFFENSIVE);
	GetStat()->SetDead(false);

	m_inventory = std::make_shared<Inventory>();
	m_inventory->Attach(self<User>());
}

shared_ptr<GameSession> User::GetGameSession() const
{
	return m_session;
}

void User::SetGameSession(const shared_ptr<GameSession>& session)
{
	m_session = session;
}

bool User::IsExtended() const noexcept
{
	return m_session != nullptr && m_session->IsExtended();
}

void User::ApplyLevelStats(bool fullHeal)
{
	if (IsExtended())
	{
		RecomputeStats(fullHeal);
		return;
	}
	const uint8_t level = GetStat()->GetLevel();
	const uint16_t maxHp = LevelTable::MaxHp(level);
	GetStat()->SetMaxHp(maxHp);
	if (fullHeal || GetStat()->GetHp() > maxHp)
		GetStat()->SetHp(maxHp);

	// 공격력 = 레벨 기본 + 장착 무기 보너스 + 강화 보너스. 계산은 장비 상태를 아는
	// Inventory가 한 곳에서 한다(장착/탈착/강화 때도 같은 함수가 불린다).
	if (m_inventory != nullptr)
		m_inventory->RecomputeOffensive();
}

uint32_t User::GetMoveSpeedPercent(uint32_t now) const noexcept
{
	// 확장 세션은 능력치(DEX)·장비(이동 속도 옵션·세트)가 반영된 이동속도를 쓴다.
	// 클라이언트는 USER_STATS_INF의 stats.moveSpeed로 같은 값을 받아 예측한다.
	uint32_t speed = IsExtended() ? m_derived.moveSpeed : 100u;
	if (IsHasted(now))
		speed += MoveRule::kHasteBonusPercent;
	return MoveRule::ClampSpeed(speed);
}

// ─────────────────────────────────────────────────────────────────────────────
// V2 능력치
// ─────────────────────────────────────────────────────────────────────────────
BaseStats User::TotalBase() const noexcept
{
	BaseStats b = ClassTable::BaseAt(m_classId, GetStatConst()->GetLevel());
	for (int i = 0; i < 6; ++i)
		b.v[i] = static_cast<int16_t>(b.v[i] + m_alloc.v[i]);
	return b;
}

uint16_t User::FreePoints() const noexcept
{
	// 레벨업마다 1포인트(2레벨부터). 찍은 만큼 뺀다.
	const int earned = GetStatConst()->GetLevel() > 1 ? GetStatConst()->GetLevel() - 1 : 0;
	int spent = 0;
	for (int i = 0; i < 6; ++i) spent += m_alloc.v[i];
	return static_cast<uint16_t>(earned > spent ? earned - spent : 0);
}

bool User::RecomputeStats(bool fullHeal)
{
	if (!IsExtended())
	{
		// 레거시 공식 그대로
		const uint8_t level = GetStat()->GetLevel();
		const uint16_t maxHp = LevelTable::MaxHp(level);
		GetStat()->SetMaxHp(maxHp);
		if (fullHeal || GetStat()->GetHp() > maxHp)
			GetStat()->SetHp(maxHp);
		if (m_inventory != nullptr)
			m_inventory->RecomputeOffensive();
		return false;
	}

	// 장비 보너스 — 착용 중인 모든 부위(무기+강화 · 방어구 · 장신구 · 옵션 · 세트).
	GearBonus gear{};
	GearExtras extras{};
	if (m_inventory != nullptr)
		m_inventory->CollectGearBonus(gear, &extras);
	m_gearEx = extras;

	const DerivedStats before = m_derived;
	m_gear = gear;
	m_derived = StatRule::Compute(m_classId, GetStat()->GetLevel(), TotalBase(), gear);

	GetStat()->SetMaxHp(m_derived.maxHp);
	if (fullHeal || GetStat()->GetHp() > m_derived.maxHp)
		GetStat()->SetHp(m_derived.maxHp);
	// 레거시 경로(공격력 하나)도 같은 값을 보게 한다.
	GetStat()->SetOffensive(m_classId == ClassId::RuneCaster ? m_derived.magicAttack : m_derived.attack);
	if (fullHeal || m_mp > m_derived.maxMp)
		m_mp = m_derived.maxMp;

	return std::memcmp(&before, &m_derived, sizeof(DerivedStats)) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// V2 진행 데이터: 'P' '2' version | classId | alloc[6](int16 LE) | (확장 필드…)
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	// 1: classId + alloc[6]
	// 2: + 드롭 천장 카운터(pityRare, pityHeroic)
	constexpr uint8_t kProgressVersion = 2;
	void Put16(std::vector<uint8_t>& out, int v) { out.push_back(static_cast<uint8_t>(v & 0xFF)); out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF)); }
	int Get16(const std::vector<uint8_t>& in, size_t at) { return static_cast<int16_t>(in[at] | (in[at + 1] << 8)); }
}

std::vector<uint8_t> User::SerializeProgress() const
{
	std::vector<uint8_t> out;
	out.reserve(64);
	out.push_back('P');
	out.push_back('2');
	out.push_back(kProgressVersion);
	out.push_back(static_cast<uint8_t>(m_classId));
	for (int i = 0; i < 6; ++i)
		Put16(out, m_alloc.v[i]);
	Put16(out, m_pityRare);
	Put16(out, m_pityHeroic);
	return out;
}

bool User::DeserializeProgress(const std::vector<uint8_t>& in)
{
	if (in.size() < 4 + 12 || in[0] != 'P' || in[1] != '2' || in[2] < 1)
		return false;
	if (ClassTable::IsValid(in[3]))
		m_classId = static_cast<ClassId>(in[3]);
	for (int i = 0; i < 6; ++i)
		m_alloc.v[i] = static_cast<int16_t>(std::clamp(Get16(in, 4 + i * 2), 0, 999));
	if (in[2] >= 2 && in.size() >= 4 + 12 + 4)
	{
		m_pityRare   = static_cast<uint16_t>(std::clamp(Get16(in, 16), 0, 1000));
		m_pityHeroic = static_cast<uint16_t>(std::clamp(Get16(in, 18), 0, 1000));
	}
	return true;
}
