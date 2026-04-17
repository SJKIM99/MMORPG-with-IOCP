#pragma once

#include "Subject.h"
#include "User.h"
#include "Monster.h"

class GameObjectManager
{
    unordered_map<ObjID, GameObject::SharedPtr> m_gameObjects;

public:
    GameObjectManager() = default;

    bool Insert(const ObjID& objID, GameObject::SharedPtr gameObject);
    bool Delete(const ObjID& objID);

    template<typename _Ty = GameObject> requires derived_from<_Ty, GameObject>
    [[nodiscard]] shared_ptr<_Ty> Seek(const ObjID& objID)
    {
        return Find<_Ty>(objID);
    }

private:
    template<typename _Ty>
    [[nodiscard]] shared_ptr<_Ty> Find(const ObjID& objID)
    {
        const auto it = m_gameObjects.find(objID);
        if (it == m_gameObjects.end())
            return nullptr;

        return Cast<_Ty>(objID, it->second);
    }

    template<typename _Ty>
    [[nodiscard]] static shared_ptr<_Ty> Cast(const ObjID& objID, const GameObject::SharedPtr& gameObject)
    {
        if (gameObject == nullptr)
            return nullptr;

        if constexpr (same_as<_Ty, User>)
        {
            if (objID.GetCategory<EnumCategory>() != EnumCategory::eUser)
                return nullptr;
            return static_pointer_cast<User>(gameObject);
        }
        else if constexpr (same_as<_Ty, Monster>)
        {
            if (objID.GetCategory<EnumCategory>() != EnumCategory::eMonster)
                return nullptr;
            return static_pointer_cast<Monster>(gameObject);
        }

        if constexpr (same_as<_Ty, GameObject>)
            return gameObject;

        return dynamic_pointer_cast<_Ty>(gameObject);
    }
};

extern shared_ptr<GameObjectManager> GGameObjectManager;

template <typename _Ty>
std::shared_ptr<_Ty> GetGameObject(const ObjID& objID)
{
    return GGameObjectManager->Seek<_Ty>(objID);
}
