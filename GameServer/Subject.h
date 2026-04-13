#pragma once

#include "Transform.h"
#include "Stat.h"
#include "GameObject.h"

class Subject : public GameObject, public Transform
{
	std::string      m_Name;
	Stat::SharedPtr  m_stat;

public:
	using SharedPtr = shared_ptr<Subject>;
	using WeakPtr = weak_ptr<Subject>;

public:
	Subject() = default;
	explicit Subject(const ObjID& objID) : GameObject(objID) {};

	std::string& GetName() { return m_Name; }
	Stat::SharedPtr GetStat() { return m_stat; }

	void SetName(const std::string& name) { m_Name = name; }

	virtual void InitInstance() override;
	virtual bool OnUpdate() override;
};

Subject::SharedPtr MakeNewSubject(const ObjID& e, const ObjID& OwnerID);


