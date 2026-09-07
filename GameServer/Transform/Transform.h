#pragma once

#include <atomic>
#include "Zone/ZoneTypes.h"

class Transform
{
public:
	using SharedPtr = shared_ptr<Transform>;
	using WeakPtr = weak_ptr<Transform>;

private:
	short m_x       = -1;
	short m_y       = -1;
	short m_sectorX = -1;
	short m_sectorY = -1;
	// 소유 Zone 스레드가 쓰지만, 읽는 쪽이 항상 소유자인 것은 아니다 —
	// 어그로 몬스터는 추격 대상이 자기 Zone을 벗어났는지 보려고 대상 플레이어의
	// 이 값을 읽는데(MonsterHelper::HandleAggroMove), 그 플레이어가 이미 다른
	// Zone으로 넘어갔다면 새 소유 스레드가 같은 값을 쓰고 있을 수 있다.
	// 생 정수로 두면 그 조합이 데이터 레이스가 되므로 원자적으로 다룬다.
	// 순서를 맞춰야 할 다른 데이터가 없는 단일 값이고 실제 동기화는 Zone 큐
	// 전달(뮤텍스)이 담당하므로 relaxed로 충분하다.
	std::atomic<ZoneId> m_zoneId{ InvalidZoneId };
	std::atomic<bool> m_transferring{ false };

public:
	Transform() = default;

	// ── Getters ──────────────────────────────────────────────
	short GetX()       const noexcept { return m_x; }
	short GetY()       const noexcept { return m_y; }
	short GetSectorX() const noexcept { return m_sectorX; }
	short GetSectorY() const noexcept { return m_sectorY; }
	ZoneId GetZoneId() const noexcept { return m_zoneId.load(std::memory_order_relaxed); }
	bool IsTransferring() const noexcept { return m_transferring.load(std::memory_order_acquire); }

	// ── Setters ──────────────────────────────────────────────
	void SetX(short x)           noexcept { m_x = x; }
	void SetY(short y)           noexcept { m_y = y; }
	void SetPosition(short x, short y) noexcept { m_x = x; m_y = y; }
	void SetSectorX(short sx)    noexcept { m_sectorX = sx; }
	void SetSectorY(short sy)    noexcept { m_sectorY = sy; }
	void SetSector(short sx, short sy) noexcept { m_sectorX = sx; m_sectorY = sy; }
	void SetZoneId(ZoneId zoneId) noexcept { m_zoneId.store(zoneId, std::memory_order_relaxed); }
	void SetTransferring(bool transferring) noexcept { m_transferring.store(transferring, std::memory_order_release); }

	// ── In-out references (Sector 업데이트 함수 전달용) ──
	short& RefX()       noexcept { return m_x; }
	short& RefY()       noexcept { return m_y; }
	short& RefSectorX() noexcept { return m_sectorX; }
	short& RefSectorY() noexcept { return m_sectorY; }

	// ── Utility ──────────────────────────────────────────────
	void Reset() noexcept;
};
