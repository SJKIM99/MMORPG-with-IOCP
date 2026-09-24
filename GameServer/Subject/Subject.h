#pragma once

#include "Transform.h"
#include "Stat.h"
#include "GameObject.h"

class Subject : public GameObject, public Transform
{
	std::string      m_Name;
	Stat::SharedPtr  m_stat;
	bool             m_facingLeft = false; // false = facing right (default)
	// 마지막 걸음에 걸린 시간(ms). SUBJECT_MOVE_NFY.move_time으로 보내 클라이언트가 그
	// 속도로 보간하게 한다. 0 = 순간이동/재배치(보간 없이 바로 옮김).
	uint16_t         m_lastStepMs = 0;

public:
	using SharedPtr = shared_ptr<Subject>;
	using WeakPtr = weak_ptr<Subject>;

public:
	Subject() = default;
	explicit Subject(const ObjID& objID) : GameObject(objID) {};

	std::string& GetName() { return m_Name; }
	Stat::SharedPtr GetStat() { return m_stat; }

	bool IsFacingLeft() const           { return m_facingLeft; }
	void SetFacingLeft(bool facingLeft) { m_facingLeft = facingLeft; }

	// Update facing from a direction delta. dx=0 → no change.
	void UpdateFacing(int dx) { if (dx != 0) m_facingLeft = (dx < 0); }

	void SetName(const std::string& name) { m_Name = name; }

	[[nodiscard]] uint16_t GetLastStepMs() const noexcept { return m_lastStepMs; }
	void SetLastStepMs(uint32_t ms) noexcept { m_lastStepMs = static_cast<uint16_t>(ms > 60000 ? 60000 : ms); }

	virtual void InitInstance() override;
};

Subject::SharedPtr MakeNewSubject(const ObjID& e, const ObjID& OwnerID);


