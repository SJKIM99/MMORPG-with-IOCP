#pragma once

#include "Subject.h"

// Manages all game objects (players + monsters) by ID.
// Replaces the old flat GClients array with an unordered_map.
// Populated once at startup; no insertions or removals during gameplay.
class GameObjectManager
{
public:
    void Register(uint32 id, shared_ptr<Subject> obj);

    shared_ptr<Subject>&       operator[](uint32 id);
    const shared_ptr<Subject>& operator[](uint32 id) const;

    bool   Contains(uint32 id) const;
    size_t Size()              const;

private:
    unordered_map<uint32, shared_ptr<Subject>> _objects;
};

extern GameObjectManager* GObjectManager;
