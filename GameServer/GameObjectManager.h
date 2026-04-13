#pragma once

#include "Subject.h"
#include <shared_mutex>

class GameObjectManager
{
    mutable shared_mutex m_lock;
    unordered_map<ObjID, GameObject::SharedPtr> m_gameObjects;

public:
    GameObjectManager() = default;

    bool Insert(const ObjID& objID, GameObject::SharedPtr gameObject);
    bool Delete(const ObjID& objID);

    template<typename _Ty = GameObject> requires derived_from<_Ty, GameObject>
    shared_ptr<_Ty> Seek(const ObjID& objID)
    {
        shared_lock lock(m_lock);
        auto it = m_gameObjects.find(objID);
        if (it == m_gameObjects.end())
            return nullptr;
        return dynamic_pointer_cast<_Ty>(it->second);
    }
};

extern shared_ptr<GameObjectManager> GGameObjectManager;

template <typename _Ty>
std::shared_ptr<_Ty> GetGameObject(const ObjID& objID)
{
    return std::dynamic_pointer_cast<_Ty>(GGameObjectManager->Seek(objID));
}

