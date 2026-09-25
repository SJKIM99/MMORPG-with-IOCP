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
	// 3: + 퀘스트(진행 중 칸 · 끝낸 퀘스트 비트 · 반복 퀘스트 대기 시각)
	constexpr uint8_t kProgressVersion = 3;
	void Put16(std::vector<uint8_t>& out, int v) { out.push_back(static_cast<uint8_t>(v & 0xFF)); out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF)); }
	void Put32(std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF)); }
	int Get16(const std::vector<uint8_t>& in, size_t at) { return static_cast<int16_t>(in[at] | (in[at + 1] << 8)); }
	uint16_t GetU16(const std::vector<uint8_t>& in, size_t at) { return static_cast<uint16_t>(in[at] | (in[at + 1] << 8)); }
	uint32_t GetU32(const std::vector<uint8_t>& in, size_t at)
	{
		uint32_t v = 0;
		for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(in[at + i]) << (i * 8);
		return v;
	}
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
	// v3: 퀘스트
	uint8_t active = 0;
	for (const auto& q : m_quests) if (q.id != 0) ++active;
	out.push_back(active);
	for (const auto& q : m_quests)
	{
		if (q.id == 0) continue;
		Put16(out, q.id);
		for (uint16_t p : q.progress) Put16(out, p);
	}
	out.insert(out.end(), m_questDone.begin(), m_questDone.end());
	const uint8_t cds = static_cast<uint8_t>((std::min)(m_questCooldowns.size(), size_t{ 32 }));
	out.push_back(cds);
	for (size_t i = 0; i < cds; ++i)
	{
		Put16(out, m_questCooldowns[i].id);
		Put32(out, m_questCooldowns[i].readyAtUnix);
	}
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
	// v3: 퀘스트 — 표에 없는 id(지워진 퀘스트)는 버리고, 진행도는 목표 수로 자른다.
	m_quests = {};
	m_questDone = {};
	m_questCooldowns.clear();
	size_t at = 20;
	if (in[2] >= 3 && in.size() >= at + 1)
	{
		const uint8_t active = in[at++];
		size_t slot = 0;
		for (uint8_t i = 0; i < active && at + 8 <= in.size(); ++i, at += 8)
		{
			const uint16_t id = GetU16(in, at);
			const QuestData::QuestRow* row = QuestData::Find(id);
			if (row == nullptr || slot >= m_quests.size())
				continue;
			bool dup = false;
			for (size_t k = 0; k < slot; ++k) dup |= m_quests[k].id == id;
			if (dup)
				continue;
			m_quests[slot].id = id;
			for (int o = 0; o < QuestData::kMaxObjs; ++o)
				m_quests[slot].progress[o] = static_cast<uint16_t>((std::min)(GetU16(in, at + 2 + o * 2), row->objs[o].count));
			++slot;
		}
		if (at + m_questDone.size() <= in.size())
		{
			std::copy(in.begin() + static_cast<std::ptrdiff_t>(at), in.begin() + static_cast<std::ptrdiff_t>(at + m_questDone.size()), m_questDone.begin());
			at += m_questDone.size();
			if (at + 1 <= in.size())
			{
				const uint8_t cds = in[at++];
				for (uint8_t i = 0; i < cds && at + 6 <= in.size(); ++i, at += 6)
					if (QuestData::Find(GetU16(in, at)) != nullptr)
						m_questCooldowns.push_back({ GetU16(in, at), GetU32(in, at + 2) });
			}
		}
	}
	return true;
}
