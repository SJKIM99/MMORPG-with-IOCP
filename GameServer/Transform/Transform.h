#pragma once

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

public:
	Transform() = default;

	// ── Getters ──────────────────────────────────────────────
	short GetX()       const noexcept { return m_x; }
	short GetY()       const noexcept { return m_y; }
	short GetSectorX() const noexcept { return m_sectorX; }
	short GetSectorY() const noexcept { return m_sectorY; }

	// ── Setters ──────────────────────────────────────────────
	void SetX(short x)           noexcept { m_x = x; }
	void SetY(short y)           noexcept { m_y = y; }
	void SetPosition(short x, short y) noexcept { m_x = x; m_y = y; }
	void SetSectorX(short sx)    noexcept { m_sectorX = sx; }
	void SetSectorY(short sy)    noexcept { m_sectorY = sy; }
	void SetSector(short sx, short sy) noexcept { m_sectorX = sx; m_sectorY = sy; }

	// ── In-out references (Sector::UpdateObjectSector / RemoveObject 전달용) ──
	short& RefSectorX() noexcept { return m_sectorX; }
	short& RefSectorY() noexcept { return m_sectorY; }

	// ── Utility ──────────────────────────────────────────────
	void Reset() noexcept;
};
