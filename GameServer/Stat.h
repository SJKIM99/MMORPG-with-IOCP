#pragma once
#include "GameObject.h"

class Stat : public GameObject
{
public:
	using SharedPtr = shared_ptr<Stat>;
	using WeakPtr = weak_ptr<Stat>;
public:
	Stat() = default;
	// atomic 멤버가 있으므로 복사 금지
	Stat(const ObjID& ownerID)
	{
		SetOwnerID(ownerID);
	}

	Stat(const Stat&)            = delete;
	Stat& operator=(const Stat&) = delete;

	virtual bool OnUpdate() override;

	static constexpr uint32 EXP_TABLE[4] = { 30, 50, 70, 90 };
	static constexpr uint8  MAX_LEVEL    = 5;

	// ── Getters ──────────────────────────────────────────────
	uint16 GetMaxHp()    const noexcept { return _maxHp; }
	uint16 GetHp()       const noexcept { return _hp.load(); }
	uint16 GetOffensive()const noexcept { return _offensive; }
	bool   IsDead()      const noexcept { return _die.load(); }
	uint8  GetLevel()    const noexcept { return _level; }
	uint32 GetExp()      const noexcept { return _exp; }

	// ── Setters ──────────────────────────────────────────────
	void SetMaxHp(uint16 maxHp)          noexcept { _maxHp = maxHp; }
	void SetHp(uint16 hp)                noexcept { _hp.store(hp); }
	void SetOffensive(uint16 offensive)  noexcept { _offensive = offensive; }
	void SetDead(bool dead)              noexcept { _die.store(dead); }
	void SetLevel(uint8 level)           noexcept { _level = level; }
	void SetExp(uint32 exp)              noexcept { _exp = exp; }

	// ── Wrapper functions ─────────────────────────────────────
	// 데미지 적용 후 남은 HP 반환. HP가 0이 되면 _die를 true로 설정
	uint16 TakeDamage(uint16 amount) noexcept;

	// HP 회복. maxCap을 초과하지 않도록 제한
	void HealHp(uint16 amount, uint16 maxCap) noexcept;

	// 경험치 추가. 레벨업 시 true 반환
	bool AddExp(uint32 amount) noexcept;

	// 모든 수치를 초기값으로 되돌림
	void Reset() noexcept;

private:
	uint16         _maxHp     = 0;
	Atomic<uint16> _hp        = 0;
	uint16         _offensive = 0;
	Atomic<bool>   _die       = true;
	uint8          _level     = 1;
	uint32         _exp       = 0;
};

