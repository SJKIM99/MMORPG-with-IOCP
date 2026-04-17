#include "pch.h"
#include "GameObject.h"
#include "GameObjectManager.h"

GameObject::GameObject(const ObjID& objID)
{
	SetObjID(objID);
}

GameObject::GameObject(const ObjID& objID, const ObjID& ownerID)
{
	SetObjID(objID);
	SetOwnerID(ownerID);
}

void GameObject::InitInstance()
{
	SetParent(nullptr);
}


void GameObject::SetParent(const SharedPtr& parent) noexcept
{
	m_parent = parent;
}