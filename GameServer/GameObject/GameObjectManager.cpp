#include "pch.h"
#include "GameObjectManager.h"

bool GameObjectManager::Insert(const ObjID& objID, GameObject::SharedPtr gameObject)
{
	unique_lock lock(m_lock);
	auto [iter, success] = m_gameObjects.emplace(objID, gameObject);

	return success;
}

bool GameObjectManager::Delete(const ObjID& objID)
{
	unique_lock lock(m_lock); 
	size_t erasedCount = m_gameObjects.erase(objID);

	return erasedCount > 0;
}

