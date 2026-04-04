#include "pch.h"
#include "User.h"
#include "TimerThread.h"
#include "WorldHelper.h"

// ================================================================
// User
// ================================================================

User::User() : _traceNpcId(-1)
{
}

void User::InitializePlayers()
{
	for (int32 i = 0; i < MAX_USER; ++i)
	{
		auto player = MakeShared<User>();
		player->InitInstance();
		GObjectManager->Register(i, player);
	}

	cout << "Sessions Init Success" << endl;
}

void User::InitInstance()
{
	Subject::InitInstance();
}

void User::ResetGameplayState()
{
	Subject::ResetGameplayState();
	_traceNpcId.store(-1);
}

void User::Heal()
{
	GTimerThread->ScheduleNow(_id, GetTimerEpoch(), TIMER_EVENT_TYPE::EV_HEAL);
}

bool User::TryBuildSaveInfo(DB_PLAYER_INFO& outPlayerInfo) const
{
	if (_state != SOCKET_STATE::ST_INGAME)
		return false;
	if (_transform.GetX() < 0 || _transform.GetY() < 0)
		return false;
	if (_name[0] == '\0')
		return false;

	outPlayerInfo._name = _name;
	outPlayerInfo._x    = _transform.GetX();
	outPlayerInfo._y    = _transform.GetY();
	return true;
}

// ================================================================
// Monster
// ================================================================

Monster::Monster() : _type(MONSTER_TYPE::AGGRO)
{
}

void Monster::InitInstance()
{
	Subject::InitInstance();
}

void Monster::ResetGameplayState()
{
	Subject::ResetGameplayState();
	_astarPath.clear();
}

void Monster::InitAll()
{
	for (int32 i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
	{
		auto monster = MakeShared<Monster>();
		monster->InitInstance();
		GObjectManager->Register(i, monster);

		monster->_id = i;
		monster->SetType(i <= AGGRO_NPC_BOUNDARY ? MONSTER_TYPE::AGGRO : MONSTER_TYPE::PASSIVE);
		monster->_stat.SetMaxHp(NPC_MAX_HP);
		monster->_stat.SetHp(NPC_MAX_HP);
		monster->_stat.SetOffensive(NPC_OFFENSIVE);
		monster->_stat.SetDead(false);
		sprintf_s(monster->_name, "NPC%d", i);
		monster->_state = ST_INGAME;
		monster->_active.store(false);
		monster->_attack.store(false);

		WorldHelper::PlaceObjectAtRandomWalkablePosition(i);
	}

	cout << "Monster Init Success" << endl;
}

void Monster::RandomMove(uint32 npcId)
{
	const auto oldList = WorldHelper::CollectVisiblePlayersAround(npcId);

	short x = (*GObjectManager)[npcId]->_transform.GetX();
	short y = (*GObjectManager)[npcId]->_transform.GetY();
	WorldHelper::MovePositionByDirection(x, y, static_cast<char>(rand() % 4));
	WorldHelper::UpdateObjectPosition(npcId, x, y);

	const auto newList = WorldHelper::CollectVisiblePlayersAround(npcId);
	WorldHelper::BroadcastNpcVisibilityDelta(npcId, oldList, newList);
}

void Monster::AStarMove(uint32 npcId, short nextX, short nextY)
{
	const auto oldList = WorldHelper::CollectVisiblePlayersAround(npcId);
	WorldHelper::UpdateObjectPosition(npcId, nextX, nextY);
	const auto newList = WorldHelper::CollectVisiblePlayersAround(npcId);
	WorldHelper::BroadcastNpcVisibilityDelta(npcId, oldList, newList);
}
