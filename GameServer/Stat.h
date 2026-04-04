#pragma once

class Stat
{
public:
	Stat() = default;
	// atomic 멤버가 있으므로 복사 금지
	Stat(const Stat&)            = delete;
	Stat& operator=(const Stat&) = delete;

	// ── Getters ──────────────────────────────────────────────
	uint16 GetMaxHp()    const noexcept { return _maxHp; }
	uint16 GetHp()       const noexcept { return _hp.load(); }
	uint16 GetOffensive()const noexcept { return _offensive; }
	bool   IsDead()      const noexcept { return _die.load(); }

	// ── Setters ──────────────────────────────────────────────
	void SetMaxHp(uint16 maxHp)          noexcept { _maxHp = maxHp; }
	void SetHp(uint16 hp)                noexcept { _hp.store(hp); }
	void SetOffensive(uint16 offensive)  noexcept { _offensive = offensive; }
	void SetDead(bool dead)              noexcept { _die.store(dead); }

	// ── Wrapper functions ─────────────────────────────────────
	// 데미지 적용 후 남은 HP 반환. HP가 0이 되면 _die를 true로 설정
	uint16 TakeDamage(uint16 amount) noexcept;

	// HP 회복. maxCap을 초과하지 않도록 제한
	void HealHp(uint16 amount, uint16 maxCap) noexcept;
	void Reset() noexcept;

	// 모든 수치를 초기값으로 되돌림
	void Reset() noexcept;

private:
	uint16         _maxHp     = 0;
	Atomic<uint16> _hp        = 0;
	uint16         _offensive = 0;
	Atomic<bool>   _die       = true;
};
