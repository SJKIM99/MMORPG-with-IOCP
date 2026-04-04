#include "pch.h"
#include "GameObjectManager.h"

void GameObjectManager::Register(uint32 id, shared_ptr<Subject> obj)
{
    _objects.emplace(id, move(obj));
}

shared_ptr<Subject>& GameObjectManager::operator[](uint32 id)
{
    auto it = _objects.find(id);
    ASSERT_CRASH(it != _objects.end());
    return it->second;
}

const shared_ptr<Subject>& GameObjectManager::operator[](uint32 id) const
{
    auto it = _objects.find(id);
    ASSERT_CRASH(it != _objects.end());
    return it->second;
}

bool GameObjectManager::Contains(uint32 id) const
{
    return _objects.count(id) > 0;
}

size_t GameObjectManager::Size() const
{
    return _objects.size();
}
