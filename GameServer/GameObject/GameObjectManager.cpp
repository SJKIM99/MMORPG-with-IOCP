#include "pch.h"
#include "GameObjectManager.h"

bool GameObjectManager::Insert(const ObjID& objID, GameObject::SharedPtr gameObject)
{
	unique_lock lock(_objectsMutex);
	auto [iter, success] = m_gameObjects.emplace(objID, gameObject);
	return success;
}

bool GameObjectManager::Delete(const ObjID& objID)
{
	unique_lock lock(_objectsMutex);
	return m_gameObjects.erase(objID) > 0;
}

