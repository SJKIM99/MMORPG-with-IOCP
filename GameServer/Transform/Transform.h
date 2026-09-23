#pragma once

#include <atomic>
#include "Vec3.h"
#include "Zone/ZoneTypes.h"

class Transform
{
public:
	using SharedPtr = shared_ptr<Transform>;
	using WeakPtr = weak_ptr<Transform>;

private:
	// 미터 단위 3D 위치. 지면은 XZ, 높이는 Y (Vec3.h 참고).
	Vec3 m_pos{};

	// 바라보는 방향. **yaw 하나만** 둔다 — 피치·롤은 클라이언트 표시용이라
	// 서버가 알 필요가 없고, 쿼터니언은 더더욱 아니다 (CLAUDE.md 3장).
	float m_yaw = 0.0f;

	// Sector 격자 인덱스. XZ 평면만 분할하므로 축이 둘이다.
	// 이름에 Y 를 쓰지 않는 이유: 새 좌표계에서 Y 는 높이고, 격자는 높이를
	// 나누지 않는다. 옛 이름을 두면 "높이 섹터"로 읽힌다.
	short m_sectorX = -1;
	short m_sectorZ = -1;

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
	const Vec3& GetPosition() const noexcept { return m_pos; }
	float GetX()       const noexcept { return m_pos.x; }
	// GetY() 는 **높이**다. 2D 시절의 y(지면 두 번째 축)를 찾는다면 GetZ() 다.
	float GetY()       const noexcept { return m_pos.y; }
	float GetZ()       const noexcept { return m_pos.z; }
	float GetYaw()     const noexcept { return m_yaw; }
	short GetSectorX() const noexcept { return m_sectorX; }
	short GetSectorZ() const noexcept { return m_sectorZ; }
	ZoneId GetZoneId() const noexcept { return m_zoneId.load(std::memory_order_relaxed); }
	bool IsTransferring() const noexcept { return m_transferring.load(std::memory_order_acquire); }

	// ── Setters ──────────────────────────────────────────────
	void SetPosition(const Vec3& pos) noexcept { m_pos = pos; }
	// 지면 좌표만 옮긴다. 높이는 지형에서 따로 구해 넣는다(Week 1 의 4번 단계).
	void SetGround(float x, float z) noexcept { m_pos.x = x; m_pos.z = z; }
	void SetX(float x)  noexcept { m_pos.x = x; }
	void SetY(float y)  noexcept { m_pos.y = y; }
	void SetZ(float z)  noexcept { m_pos.z = z; }
	void SetYaw(float yaw) noexcept { m_yaw = yaw; }
	void SetSectorX(short sx)    noexcept { m_sectorX = sx; }
	void SetSectorZ(short sz)    noexcept { m_sectorZ = sz; }
	void SetSector(short sx, short sz) noexcept { m_sectorX = sx; m_sectorZ = sz; }
	void SetZoneId(ZoneId zoneId) noexcept { m_zoneId.store(zoneId, std::memory_order_relaxed); }
	void SetTransferring(bool transferring) noexcept { m_transferring.store(transferring, std::memory_order_release); }

	// ── In-out references (Sector 업데이트 함수 전달용) ──
	float& RefX()       noexcept { return m_pos.x; }
	float& RefZ()       noexcept { return m_pos.z; }
	short& RefSectorX() noexcept { return m_sectorX; }
	short& RefSectorZ() noexcept { return m_sectorZ; }

	// ── Utility ──────────────────────────────────────────────
	void Reset() noexcept;
};
