#pragma once

#include "Transform.h"
#include "Stat.h"
#include "GameObject.h"

class Subject : public GameObject, public Transform
{
	std::string      m_Name;
	Stat::SharedPtr  m_stat;
	bool             m_facingLeft = false; // false = facing right (default)

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

	virtual void InitInstance() override;
};

Subject::SharedPtr MakeNewSubject(const ObjID& e, const ObjID& OwnerID);


