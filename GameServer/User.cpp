#include "pch.h"
#include "User.h"
#include "TimerThread.h"
#include "WorkerThread.h"

array<shared_ptr<User>, MAX_USER + MAX_NPC> GClients;

namespace
{
	template<typename Packet>
	void InitializePacket(Packet& packet, PacketType type)
	{
		packet = {};
		packet.size = sizeof(Packet);
		packet.type = static_cast<char>(type);
	}
}

User::User()
{
	::memset(_name, 0, sizeof(_name));
}

void User::InitializePlayers()
{
	for (int32 i = 0; i < MAX_USER; ++i)
	{
		auto player = MakeShared<Player>();
		player->InitInstance();
		GClients[i] = move(player);
	}

	cout << "Sessions Init Success" << endl;
}

void User::InitInstance()
{
	GameObject::InitInstance();
	ResetGameplayState();
}

void User::OnUpdate(const UpdateTimePoint& updateTime)
{
	GameObject::OnUpdate(updateTime);
}

void User::ResetGameplayState()
{
	_timerEpoch.fetch_add(1);
	if (_state != SOCKET_STATE::ST_FREE)
		_state = SOCKET_STATE::ST_ALLOC;
	_x = -1;
	_y = -1;
	_hp.store(0);
	_lastMoveTime = 0;
	_sectorX = -1;
	_sectorY = -1;
	_viewList.clear();
	_active.store(false);
	_die.store(true);
	_attack.store(false);
}

void User::SendMovePacket(uint32 clientId)
{
	const User& target = *GClients[clientId];

	SC_MOVE_OBJECT_PACKET packet;
	InitializePacket(packet, PacketType::SC_MOVE_OBJECT);
	packet.id = clientId;
	packet.x = target._x;
	packet.y = target._y;
	packet.move_time = target._lastMoveTime;

	PostSend(packet);
}

void User::SendAddPlayerPacket(uint32 clientId)
{
	_viewList.insert(clientId);

	const User& target = *GClients[clientId];

	SC_ADD_OBJECT_PACKET packet;
	InitializePacket(packet, PacketType::SC_ADD_OBJECT);
	if (IsNPC(clientId))
		packet.monster_type = static_cast<char>(AsMonster(clientId)->GetType());
	packet.id = clientId;
	packet.x = target._x;
	packet.y = target._y;
	::strcpy_s(packet.name, target._name);

	PostSend(packet);
}

void User::SendRemovePlayerPacket(uint32 clientId)
{
	if (_viewList.erase(clientId) == 0)
		return;

	SC_REMOVE_OBJECT_PACKET packet;
	InitializePacket(packet, PacketType::SC_REMOVE_OBJECT);
	packet.id = clientId;

	PostSend(packet);
}

void User::SendLoginSuccessPacket()
{
	SC_LOGIN_SUCCESS_PACKET packet;
	InitializePacket(packet, PacketType::SC_LOGIN_SUCCESS);
	packet.id = _id;
	packet.x = _x;
	packet.y = _y;
	packet.maxhp = _maxHp;
	packet.hp = _hp.load();

	PostSend(packet);
}

void User::SendPlayerAtackToNPCPacket(uint32 clientId)
{
	SC_PLAYER_ATTACK_NPC_PACKET packet;
	InitializePacket(packet, PacketType::SC_PLAYER_ATTACK_NPC);
	packet.id = clientId;
	packet.hp = GClients[clientId]->_hp.load();

	PostSend(packet);
}

void User::SendNPCDiePacket(uint32 clientId)
{
	SC_NPC_DIE_PACKET packet;
	InitializePacket(packet, PacketType::SC_NPC_DIE);
	packet.npc_id = clientId;

	PostSend(packet);
}

void User::SendRespawnNPCPacket(uint32 clientId)
{
	const User& target = *GClients[clientId];

	SC_NPC_RESPAWN_PACKET packet;
	InitializePacket(packet, PacketType::SC_NPC_RESPAWN);
	packet.npc_id = clientId;
	packet.x = target._x;
	packet.y = target._y;

	PostSend(packet);
}

void User::SendNPCAttackToPlayerPacket(uint32 clientId)
{
	SC_NPC_ATTACK_PLAYER_PACKET packet;
	InitializePacket(packet, PacketType::SC_NPC_ATTACK_PLAYER);
	packet.hp = _hp.load();

	PostSend(packet);
}

void User::SendHealPacket()
{
	SC_HEAL_PACKET packet;
	InitializePacket(packet, PacketType::SC_HEAL);
	packet.hp = _hp.load();

	PostSend(packet);
}

void User::SendPlayerDiePacket(uint32 clientId)
{
	if (_viewList.erase(clientId) == 0)
		return;

	SC_PLAYER_DIE_PACKET packet;
	InitializePacket(packet, PacketType::SC_PLAYER_DIE);
	packet.id = clientId;
	packet.hp = GClients[clientId]->_hp.load();

	PostSend(packet);
}

void User::SendRespawnPlayerPacket(uint32 clientId)
{
	_viewList.insert(clientId);

	const User& target = *GClients[clientId];

	SC_PLAYER_RESPAWN_PACKET packet;
	InitializePacket(packet, PacketType::SC_PLAYER_RESPAWN);
	packet.id = clientId;
	packet.x = target._x;
	packet.y = target._y;
	packet.hp = target._hp.load();

	PostSend(packet);
}

Player::Player() : _traceNpcId(-1)
{
}

void Player::InitInstance()
{
	User::InitInstance();
}

void Player::ResetGameplayState()
{
	User::ResetGameplayState();
	_traceNpcId.store(-1);
}

void Player::Heal()
{
	GTimerThread->ScheduleNow(_id, GetTimerEpoch(), TIMER_EVENT_TYPE::EV_HEAL);
}

bool Player::TryBuildSaveInfo(DB_PLAYER_INFO& outPlayerInfo) const
{
	if (_state != SOCKET_STATE::ST_INGAME)
		return false;
	if (_x < 0 || _y < 0)
		return false;
	if (_name[0] == '\0')
		return false;

	outPlayerInfo._name = _name;
	outPlayerInfo._x = _x;
	outPlayerInfo._y = _y;
	return true;
}

Monster::Monster() : _type(MONSTER_TYPE::AGGRO)
{
}

void Monster::InitInstance()
{
	User::InitInstance();
}

void Monster::ResetGameplayState()
{
	User::ResetGameplayState();
	_astarPath.clear();
}
