#include "pch.h"
#include "GameObjectManager.h"

bool GameObjectManager::Insert(const ObjID& objID, GameObject::SharedPtr gameObject)
{
	auto [iter, success] = m_gameObjects.emplace(objID, gameObject);
	return success;
}

bool GameObjectManager::Delete(const ObjID& objID)
{
	return m_gameObjects.erase(objID) > 0;
}

