#pragma once

class Transform
{
public:
	Transform() = default;

	// ── Getters ──────────────────────────────────────────────
	short GetX()       const noexcept { return _x; }
	short GetY()       const noexcept { return _y; }
	short GetSectorX() const noexcept { return _sectorX; }
	short GetSectorY() const noexcept { return _sectorY; }

	// ── Setters ──────────────────────────────────────────────
	void SetX(short x)           noexcept { _x = x; }
	void SetY(short y)           noexcept { _y = y; }
	void SetPosition(short x, short y) noexcept { _x = x; _y = y; }
	void SetSectorX(short sx)    noexcept { _sectorX = sx; }
	void SetSectorY(short sy)    noexcept { _sectorY = sy; }
	void SetSector(short sx, short sy) noexcept { _sectorX = sx; _sectorY = sy; }

	// ── In-out references (Sector::UpdateObjectSector / RemoveObject 전달용) ──
	short& RefSectorX() noexcept { return _sectorX; }
	short& RefSectorY() noexcept { return _sectorY; }

	// ── Utility ──────────────────────────────────────────────
	void Reset() noexcept;

private:
	short _x       = -1;
	short _y       = -1;
	short _sectorX = -1;
	short _sectorY = -1;
};
