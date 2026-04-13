#include "pch.h"
#include "Subject.h"
#include "Monster.h"

void Subject::InitInstance()
{
	GameObject::InitInstance();
	m_stat = std::make_shared<Stat>();
}

bool Subject::OnUpdate()
{
	GameObject::OnUpdate();

	return true;
}

Subject::SharedPtr MakeNewSubject(const ObjID& e, const ObjID& OwnerID)
{
	Subject::SharedPtr newSubject{};

	switch (e.GetCategory())
	{
	case EnumCategory::eMonster:
		newSubject = std::make_shared<Monster>(e);
		if (newSubject)
		{
			newSubject->SetOwnerID(OwnerID);
			newSubject->InitInstance();
		}
		break;
	}

	return newSubject;
}
