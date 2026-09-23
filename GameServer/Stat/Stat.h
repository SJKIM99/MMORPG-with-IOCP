#pragma once
#include "GameObject.h"
#include "Data/LevelTable.h"

class Stat : public GameObject
{
public:
	using SharedPtr = shared_ptr<Stat>;
	using WeakPtr = weak_ptr<Stat>;

	// 레벨 규칙은 Data/LevelTable.h(서버/클라 공용)로 옮겼다. 예전 상수
	// (EXP_TABLE[4] = {30,50,70,90}, MAX_LEVEL = 5)는 LevelTable::ExpToNext(1) == 30에서
	// 시작하도록 맞춰 두었다.
	static constexpr uint8_t MAX_LEVEL = LevelTable::kMaxLevel;

private:
	uint16_t         m_maxHp     = 0;
	std::atomic<uint16_t> m_hp   = 0;
	uint16_t         m_offensive = 0;
	std::atomic<bool>     m_die  = true;
	uint8_t          m_level     = 1;
	uint32_t         m_exp       = 0;

public:
	Stat() = default;
	// atomic 멤버가 있으므로 복사 금지
	Stat(const ObjID& ownerID)
	{
		SetOwnerID(ownerID);
	}

	Stat(const Stat&)            = delete;
	Stat& operator=(const Stat&) = delete;

	// ── Getters ──────────────────────────────────────────────
	uint16_t GetMaxHp()    const noexcept { return m_maxHp; }
	uint16_t GetHp()       const noexcept { return m_hp.load(); }
	uint16_t GetOffensive()const noexcept { return m_offensive; }
	bool   IsDead()      const noexcept { return m_die.load(); }
	uint8_t  GetLevel()    const noexcept { return m_level; }
	uint32_t GetExp()      const noexcept { return m_exp; }

	// ── Setters ──────────────────────────────────────────────
	void SetMaxHp(uint16_t maxHp)          noexcept { m_maxHp = maxHp; }
	void SetHp(uint16_t hp)                noexcept { m_hp.store(hp); }
	void SetOffensive(uint16_t offensive)  noexcept { m_offensive = offensive; }
	void SetDead(bool dead)              noexcept { m_die.store(dead); }
	void SetLevel(uint8_t level)           noexcept { m_level = level; }
	void SetExp(uint32_t exp)              noexcept { m_exp = exp; }

	// ── Wrapper functions ─────────────────────────────────────
	// 데미지 적용 후 남은 HP 반환. HP가 0이 되면 m_die를 true로 설정
	uint16_t TakeDamage(uint16_t amount) noexcept;
	// HP 회복. maxCap을 초과하지 않도록 제한
	void HealHp(uint16_t amount, uint16_t maxCap) noexcept;

	// 경험치 추가. 이번 호출로 오른 레벨 수를 반환한다(0 = 레벨업 없음).
	// 보스처럼 큰 경험치를 한 번에 주면 여러 레벨이 한꺼번에 오를 수 있어서
	// 예전처럼 bool이 아니라 횟수를 돌려준다.
	uint8_t AddExp(uint32_t amount) noexcept;

	// 경험치 차감(사망 페널티). 현재 레벨의 누적 경험치 안에서만 깎고 레벨은 내리지
	// 않는다. 실제로 깎인 양을 반환한다.
	uint32_t LoseExp(uint32_t amount) noexcept;

	// 모든 수치를 초기값으로 되돌림
	void Reset() noexcept;
};
