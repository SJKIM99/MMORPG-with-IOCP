#pragma once

#include <array>
#include <cstdint>
#include "Data/CombatData.h"
#include "ObjID.h"

// ─────────────────────────────────────────────────────────────────────────────
// 개체(플레이어/몬스터) 하나에 걸린 상태이상. 소유 Zone 스레드에서만 읽고 쓴다.
// 시각은 GetNowTime()(ms) 기준. 슬롯이 적어 선형 탐색이면 충분하다.
// ─────────────────────────────────────────────────────────────────────────────
struct StatusSlot
{
	StatusId id = StatusId::None;
	uint8_t  stacks = 0;
	uint32_t until = 0;
	uint32_t nextTick = 0;
	uint16_t tickDamage = 0;     // 중첩 하나당 틱 피해(걸 때의 공격력으로 고정)
	ObjID    source{};
};

class StatusSet
{
public:
	static constexpr int kMax = 6;

	[[nodiscard]] const StatusSlot* Get(StatusId id, uint32_t now) const noexcept
	{
		for (const auto& s : m_slots)
			if (s.id == id && s.stacks > 0 && now < s.until)
				return &s;
		return nullptr;
	}
	[[nodiscard]] bool Has(StatusId id, uint32_t now) const noexcept { return Get(id, now) != nullptr; }
	[[nodiscard]] uint8_t Stacks(StatusId id, uint32_t now) const noexcept
	{
		const StatusSlot* s = Get(id, now);
		return s ? s->stacks : 0;
	}
	[[nodiscard]] uint32_t Remaining(StatusId id, uint32_t now) const noexcept
	{
		const StatusSlot* s = Get(id, now);
		return s ? s->until - now : 0;
	}

	// 걸기: 같은 상태면 시간 갱신 + 중첩(최대치까지). 반환값 = 결과 슬롯.
	StatusSlot& Apply(StatusId id, uint32_t now, uint32_t durationMs, uint8_t stacks, const ObjID& source, uint16_t tickDamage) noexcept
	{
		const StatusRow& row = StatusTable::Get(id);
		StatusSlot* slot = nullptr;
		for (auto& s : m_slots)
			if (s.id == id) { slot = &s; break; }
		if (slot == nullptr)
		{
			// 빈(또는 만료된) 슬롯, 없으면 가장 먼저 끝나는 슬롯을 덮어쓴다.
			for (auto& s : m_slots)
				if (s.id == StatusId::None || s.stacks == 0 || now >= s.until) { slot = &s; break; }
			if (slot == nullptr)
			{
				slot = &m_slots[0];
				for (auto& s : m_slots)
					if (s.until < slot->until) slot = &s;
			}
			*slot = StatusSlot{};
			slot->id = id;
		}
		const bool alive = slot->stacks > 0 && now < slot->until;
		const uint8_t maxStacks = row.maxStacks > 0 ? row.maxStacks : 1;
		const uint8_t base = alive ? slot->stacks : 0;
		slot->stacks = static_cast<uint8_t>(base + stacks > maxStacks ? maxStacks : base + stacks);
		slot->until = now + durationMs;
		slot->source = source;
		if (tickDamage > 0) slot->tickDamage = tickDamage;
		if (!alive && row.tickMs > 0)
			slot->nextTick = now + row.tickMs;
		return *slot;
	}

	void Remove(StatusId id) noexcept
	{
		for (auto& s : m_slots)
			if (s.id == id) s = StatusSlot{};
	}

	void Clear() noexcept { m_slots = {}; }

	[[nodiscard]] bool BlocksMove(uint32_t now) const noexcept
	{
		for (const auto& s : m_slots)
			if (s.stacks > 0 && now < s.until && StatusTable::Get(s.id).blocksMove) return true;
		return false;
	}
	[[nodiscard]] bool BlocksAttack(uint32_t now) const noexcept
	{
		for (const auto& s : m_slots)
			if (s.stacks > 0 && now < s.until && StatusTable::Get(s.id).blocksAttack) return true;
		return false;
	}
	[[nodiscard]] uint32_t SlowPercent(uint32_t now) const noexcept
	{
		uint32_t best = 0;
		for (const auto& s : m_slots)
			if (s.stacks > 0 && now < s.until)
			{
				const uint32_t p = StatusTable::Get(s.id).slowPercent;
				if (p > best) best = p;
			}
		return best;
	}
	[[nodiscard]] bool Any(uint32_t now) const noexcept
	{
		for (const auto& s : m_slots)
			if (s.stacks > 0 && now < s.until) return true;
		return false;
	}

	std::array<StatusSlot, kMax>& Slots() noexcept { return m_slots; }

private:
	std::array<StatusSlot, kMax> m_slots{};
};
