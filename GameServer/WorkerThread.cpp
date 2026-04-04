#include "pch.h"
#include "WorkerThread.h"
#include "SocketManager.h"
#include "User.h"
#include "DBThread.h"
#include "GameLogicThread.h"
#include "GameSessionManager.h"
#include "Sector.h"
#include "TimerThread.h"
#include "User.h"
#include "AStar.h"
#include "UserHelper.h"
#include "WorldHelper.h"

namespace
{
	constexpr uint32 INVALID_CLIENT_ID = static_cast<uint32>(-1);

	void ReleaseIoContext(IoContext* context)
	{
		if (context == nullptr)
			return;
		if (context->_type == IO_TYPE::IO_SEND)
			xdelete(reinterpret_cast<SendContext*>(context));
	}

	void PostAcceptRequest()
	{
		GAcceptContext.ResetOverlapped();
		DWORD bytesReceived = 0;
		SocketManager::AcceptEx(
			gListenSocket, GClientSocket,
			GAcceptContext._buffer, 0,
			sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
			&bytesReceived, static_cast<LPOVERLAPPED>(&GAcceptContext._over));
	}

	vector<char> CopyPacketBuffer(const char* packet, uint32 packetSize)
	{
		vector<char> buffer(packetSize);
		::memcpy(buffer.data(), packet, packetSize);
		return buffer;
	}

	bool IsActiveSession(uint32 clientId, uint64 sessionToken)
	{
		return clientId < MAX_USER
			&& (*GObjectManager)[clientId] != nullptr
			&& (*GObjectManager)[clientId]->GetSessionToken() == sessionToken;
	}

	bool IsCompletionForActiveSession(uint32 clientId, const IoContext* context)
	{
		if (context == nullptr) return false;
		return IsActiveSession(clientId, context->_sessionToken);
	}

	void ResetAcceptSocket()
	{
		SocketManager::Close(GClientSocket);
		GClientSocket = SocketManager::CreateSocket();
	}
}

// ================================================================

void WorkerThread::Disconnect(uint32 clientId, uint64 sessionToken)
{
	if (IsActiveSession(clientId, sessionToken) == false) return;

	auto& player = (*GObjectManager)[clientId];
	GSector->ForEachNeighborObject(player->_transform.GetSectorX(), player->_transform.GetSectorY(), [&](uint32 id)
	{
		const auto& object = (*GObjectManager)[id];
		if (id == clientId) return;
		if (IsNPC(id)) return;
		if (object->_state != SOCKET_STATE::ST_INGAME) return;
		if (!WorldHelper::CanSee(object->_id, clientId)) return;
		UserHelper::SendRemovePlayerPacket(*object, clientId);
	});

	GSector->RemoveObject(clientId, player->_transform.RefSectorX(), player->_transform.RefSectorY());
	(void)UserHelper::FlushPlayerSave(clientId);
	player->ResetGameplayState();
	player->CloseSession();
	if (IsPc(clientId))
		GSessionManager->ReleasePlayerSessionId(clientId);
}

void WorkerThread::DoWork()
{
	while (true)
	{
		DWORD numOfBytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over = nullptr;

		BOOL ret = ::GetQueuedCompletionStatus(gHandle, &numOfBytes, &key, &over, INFINITE);
		IoContext* ioContext = reinterpret_cast<IoContext*>(over);

		if (ioContext == nullptr) continue;

		if (FALSE == ret)
		{
			if (ioContext->_type == IO_TYPE::IO_ACCEPT)
			{
				std::cout << "Accept Error\n";
			}
			else
			{
				const uint32 clientId = static_cast<uint32>(key);
				std::cout << "GQCS Error on Client[" << clientId << "]\n";
				if (IsCompletionForActiveSession(clientId, ioContext))
				{
					const uint64 token = ioContext->_sessionToken;
					(*GObjectManager)[clientId]->CloseSocket();
					GGameLogicThread->Enqueue([this, clientId, token]() { Disconnect(clientId, token); });
				}
				ReleaseIoContext(ioContext);
				continue;
			}
		}

		if (numOfBytes == 0
			&& (ioContext->_type == IO_TYPE::IO_RECV || ioContext->_type == IO_TYPE::IO_SEND))
		{
			const uint32 clientId = static_cast<uint32>(key);
			if (IsCompletionForActiveSession(clientId, ioContext))
			{
				const uint64 token = ioContext->_sessionToken;
				(*GObjectManager)[clientId]->CloseSocket();
				GGameLogicThread->Enqueue([this, clientId, token]() { Disconnect(clientId, token); });
			}
			ReleaseIoContext(ioContext);
			continue;
		}

		switch (ioContext->_type)
		{
		case IO_TYPE::IO_ACCEPT:
		{
			uint32 clientId = GetNewClientId();
			if (clientId != INVALID_CLIENT_ID)
			{
				Subject* client = (*GObjectManager)[clientId].get();
				if (client->AttachSocket(GClientSocket, clientId) == false)
				{
					GSessionManager->ReleasePlayerSessionId(clientId);
					ResetAcceptSocket();
					PostAcceptRequest();
					break;
				}
				const uint64 token = client->GetSessionToken();
				::setsockopt(GClientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
					reinterpret_cast<const char*>(&gListenSocket), sizeof(gListenSocket));
				::CreateIoCompletionPort(reinterpret_cast<HANDLE>(GClientSocket), gHandle, clientId, 0);

				if (client->PostRecv() == false)
				{
					client->CloseSocket();
					GGameLogicThread->Enqueue([this, clientId, token]() { Disconnect(clientId, token); });
				}
				else
				{
					GGameLogicThread->Enqueue([this, clientId, token]() { InitializeConnectedClient(clientId, token); });
				}
				GClientSocket = SocketManager::CreateSocket();
			}
			else
			{
				std::cout << "MAX user exceeded\n";
				ResetAcceptSocket();
			}
			PostAcceptRequest();
			break;
		}
		case IO_TYPE::IO_RECV:
		{
			const uint32 clientId = static_cast<uint32>(key);
			if (IsCompletionForActiveSession(clientId, ioContext) == false) break;

			RecvContext* recvContext = static_cast<RecvContext*>(ioContext);
			uint32 remainData = numOfBytes + (*GObjectManager)[clientId]->_pendingRecvBytes;
			char* p = recvContext->_buffer;

			while (remainData > 0)
			{
				if (remainData < sizeof(uint16)) break;
				const uint16 packetSize = *reinterpret_cast<const uint16*>(p);
				if (packetSize == 0) break;
				if (packetSize > remainData) break;

				const uint64 token = ioContext->_sessionToken;
				vector<char> copy  = CopyPacketBuffer(p, packetSize);
				GGameLogicThread->Enqueue([this, clientId, token, packet = move(copy)]() mutable
				{
					if (IsActiveSession(clientId, token))
						HandlePacket(clientId, packet.data());
				});
				p           += packetSize;
				remainData  -= packetSize;
			}

			(*GObjectManager)[clientId]->_pendingRecvBytes = remainData;
			if (remainData > 0)
				::memcpy(recvContext->_buffer, p, remainData);

			if ((*GObjectManager)[clientId]->PostRecv() == false)
			{
				const uint64 token = ioContext->_sessionToken;
				(*GObjectManager)[clientId]->CloseSocket();
				GGameLogicThread->Enqueue([this, clientId, token]() { Disconnect(clientId, token); });
			}
			break;
		}
		case IO_TYPE::IO_SEND:
			ReleaseIoContext(ioContext);
			break;
		}
	}
}

void WorkerThread::InitializeConnectedClient(uint32 clientId, uint64 sessionToken)
{
	if (IsActiveSession(clientId, sessionToken) == false) return;

	Subject* client = (*GObjectManager)[clientId].get();
	client->ResetGameplayState();
	client->_stat.SetMaxHp(PLAYER_MAX_HP);
	client->_stat.SetHp(PLAYER_MAX_HP);
	client->_stat.SetOffensive(PLAYER_OFFENSIVE);
	client->_stat.SetDead(false);
	client->_state = SOCKET_STATE::ST_ALLOC;
}

void WorkerThread::HandleLoginFail(uint32 clientId, uint64 sessionToken)
{
	if (IsActiveSession(clientId, sessionToken) == false) return;
	UserHelper::SendLoginFailPacket(*(*GObjectManager)[clientId]);
}

void WorkerThread::HandleGetPlayerInfo(uint32 clientId, uint64 sessionToken, const DB_PLAYER_INFO& playerInfo)
{
	if (IsActiveSession(clientId, sessionToken) == false) return;
	if ((*GObjectManager)[clientId]->_state != SOCKET_STATE::ST_ALLOC) return;

	auto& client = (*GObjectManager)[clientId];
	::strcpy_s(client->_name, playerInfo._name.c_str());
	WorldHelper::UpdateObjectPosition(
		clientId,
		static_cast<short>(playerInfo._x),
		static_cast<short>(playerInfo._y));
	client->_state = SOCKET_STATE::ST_INGAME;

	UserHelper::SendLoginSuccessPacket(*client);
	AsUser(clientId)->Heal();
	WorldHelper::NotifyPlayerEnteredWorld(clientId, false);
}

void WorkerThread::HandleAddPlayerInfo(uint32 clientId, uint64 sessionToken, const DB_PLAYER_INFO& playerInfo)
{
	if (IsActiveSession(clientId, sessionToken) == false) return;
	if ((*GObjectManager)[clientId]->_state != SOCKET_STATE::ST_ALLOC) return;

	auto& client = (*GObjectManager)[clientId];
	::strcpy_s(client->_name, playerInfo._name.c_str());
	WorldHelper::PlaceObjectAtRandomWalkablePosition(clientId);
	client->_state = SOCKET_STATE::ST_INGAME;

	UserHelper::SendLoginSuccessPacket(*client);
	AsUser(clientId)->Heal();

	DB_PLAYER_INFO save{};
	save._name     = client->_name;
	save._password = playerInfo._password;
	save._x        = client->_transform.GetX();
	save._y        = client->_transform.GetY();
	GDBThread->RequestAddPlayer(clientId, save);

	WorldHelper::NotifyPlayerEnteredWorld(clientId, false);
}

void WorkerThread::HandleNpcRandomMove(uint32 npcId)
{
	auto& npc = (*GObjectManager)[npcId];
	const uint64 epoch = npc->GetTimerEpoch();
	bool keepGoing = false;

	GSector->ForEachNeighborObject(npc->_transform.GetSectorX(), npc->_transform.GetSectorY(), [&](uint32 id)
	{
		if ((*GObjectManager)[id]->_state != ST_INGAME) return;
		if (IsNPC(id)) return;
		if (!WorldHelper::CanSee(npcId, id)) return;
		if (!WorldHelper::CanAttack(npcId, id)) { keepGoing = true; return; }
		GTimerThread->ScheduleNow(npcId, epoch, TIMER_EVENT_TYPE::EV_NPC_ATTACK_TO_PLAYER, id);
	});

	if (keepGoing)
	{
		Monster::RandomMove(npcId);
		GTimerThread->ScheduleAfter(npcId, epoch, 1s, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
	}
	else
	{
		npc->_active.store(false);
	}
}

void WorkerThread::HandleNpcRespawn(uint32 npcId)
{
	auto& npc = (*GObjectManager)[npcId];
	WorldHelper::PlaceObjectAtRandomWalkablePosition(npcId);
	npc->_stat.SetDead(false);
	npc->_stat.SetHp(NPC_MAX_HP);
	npc->_state = SOCKET_STATE::ST_INGAME;

	GSector->ForEachNeighborObject(npc->_transform.GetSectorX(), npc->_transform.GetSectorY(), [&](uint32 id)
	{
		if ((*GObjectManager)[id]->_state != SOCKET_STATE::ST_INGAME) return;
		if (IsNPC(id)) return;
		if (WorldHelper::CanSee(npcId, id)) UserHelper::SendRespawnNpcPacket(*(*GObjectManager)[id], npcId);
	});
}

void WorkerThread::HandlePlayerRespawn(uint32 playerId)
{
	auto& player = (*GObjectManager)[playerId];
	WorldHelper::PlaceObjectAtRandomWalkablePosition(playerId);
	player->_stat.SetDead(false);
	player->_stat.SetHp(PLAYER_MAX_HP);
	player->_state = SOCKET_STATE::ST_INGAME;

	WorldHelper::NotifyPlayerEnteredWorld(playerId, true);
	AsUser(playerId)->Heal();
}

void WorkerThread::HandleNpcAggroMove(uint32 npcId, uint32 aiTargetId)
{
	if ((*GObjectManager)[aiTargetId]->_state != ST_INGAME)
	{
		(*GObjectManager)[npcId]->_active.store(false);
		(*GObjectManager)[npcId]->_attack.store(false);
		return;
	}

	Monster*     monster = AsMonster(npcId);
	const uint64 epoch   = monster->GetTimerEpoch();
	vector<NODE>& path   = monster->GetPath();

	if (path.empty())
		path = FindPath(
			(*GObjectManager)[npcId]->_transform.GetX(),    (*GObjectManager)[npcId]->_transform.GetY(),
			(*GObjectManager)[aiTargetId]->_transform.GetX(), (*GObjectManager)[aiTargetId]->_transform.GetY());

	if (!path.empty())
	{
		const short nx = path.back()._x;
		const short ny = path.back()._y;
		path.pop_back();
		Monster::AStarMove(npcId, nx, ny);
	}

	if (WorldHelper::CanAttack(npcId, aiTargetId) && !(*GObjectManager)[npcId]->_attack.load())
		GTimerThread->ScheduleNow(npcId, epoch, TIMER_EVENT_TYPE::EV_NPC_ATTACK_TO_PLAYER, aiTargetId);

	if (WorldHelper::CanSee(npcId, aiTargetId))
	{
		(*GObjectManager)[npcId]->_active.store(true);
		GTimerThread->ScheduleAfter(npcId, epoch, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, aiTargetId);
	}
	else
	{
		(*GObjectManager)[npcId]->_active.store(false);
	}
}

void WorkerThread::HandleHeal(uint32 playerId)
{
	auto& player = (*GObjectManager)[playerId];
	if (player->_stat.IsDead()) return;

	player->_stat.HealHp(HEAL_SIZE, PLAYER_MAX_HP);
	UserHelper::SendHealPacket(*player);
	GTimerThread->ScheduleAfter(playerId, player->GetTimerEpoch(), 5s, TIMER_EVENT_TYPE::EV_HEAL);
}

void WorkerThread::HandleNpcAttackToPlayer(uint32 npcId, uint32 playerId)
{
	User*    victim  = AsUser(playerId);
	Monster* monster = AsMonster(npcId);

	monster->_attack.store(true);

	if (victim->_stat.IsDead() || !WorldHelper::CanAttack(playerId, npcId) || monster->_stat.IsDead())
	{
		monster->_attack.store(false);
		return;
	}

	const uint16 remaining = victim->_stat.TakeDamage(NPC_OFFENSIVE);

	if (!victim->_stat.IsDead())
	{
		// Safe to iterate directly: all game logic runs on the single GameLogicThread.
		for (uint32 id : victim->_viewList)
		{
			if ((*GObjectManager)[id]->_state != SOCKET_STATE::ST_INGAME) continue;
			if (!WorldHelper::CanSee(playerId, id)) continue;
			if (IsPc((*GObjectManager)[id]->_id)) UserHelper::SendNpcAttackToPlayerPacket(*(*GObjectManager)[id]);
		}
		if (WorldHelper::CanAttack(npcId, playerId))
			GTimerThread->ScheduleAfter(npcId, monster->GetTimerEpoch(), 1s, TIMER_EVENT_TYPE::EV_NPC_ATTACK_TO_PLAYER, playerId);
		else
			monster->_attack.store(false);
		return;
	}

	// SendPlayerDiePacket modifies the RECEIVER's viewList ((*GObjectManager)[id]), not victim's.
	// So iterating victim->_viewList directly is safe without a copy.
	for (uint32 id : victim->_viewList)
	{
		if ((*GObjectManager)[id]->_state != SOCKET_STATE::ST_INGAME) continue;
		if (!WorldHelper::CanSee(playerId, id)) continue;
		if (IsPc((*GObjectManager)[id]->_id)) UserHelper::SendPlayerDiePacket(*(*GObjectManager)[id], playerId);
	}

	monster->_attack.store(false);
	monster->_active.store(false);
	GSector->RemoveObject(playerId, victim->_transform.RefSectorX(), victim->_transform.RefSectorY());
	victim->ResetGameplayState();
	GTimerThread->ScheduleAfter(victim->_id, victim->GetTimerEpoch(), 30s, TIMER_EVENT_TYPE::EV_PLAYER_RESPAWN);

	(void)remaining;
}

uint32 WorkerThread::GetNewClientId()
{
	return GSessionManager->AcquirePlayerSessionId();
}

void WorkerThread::HandlePacket(uint32 clientId, char* packet)
{
	switch (packet[2])
	{
	case static_cast<char>(PacketType::CS_LOGIN):
	{
		if ((*GObjectManager)[clientId]->_state != SOCKET_STATE::ST_ALLOC) break;

		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		char name[NAME_SIZE + 1]{};
		char password[PASSWORD_SIZE + 1]{};
		::strncpy_s(name,     p->name,     NAME_SIZE);
		::strncpy_s(password, p->password, PASSWORD_SIZE);
		GDBThread->RequestLogin(clientId, (*GObjectManager)[clientId]->GetSessionToken(), name, password);
		break;
	}
	case static_cast<char>(PacketType::CS_MOVE):
	{
		if ((*GObjectManager)[clientId]->_state != SOCKET_STATE::ST_INGAME) break;
		if ((*GObjectManager)[clientId]->_stat.IsDead()) break;

		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		if (p->direction > 3) break;

		const uint32 now = WorldHelper::GetNowTime();
		if (now > (*GObjectManager)[clientId]->_lastMoveTime + 1000)
		{
			(*GObjectManager)[clientId]->_lastMoveTime = p->move_time;

			short x = (*GObjectManager)[clientId]->_transform.GetX();
			short y = (*GObjectManager)[clientId]->_transform.GetY();
			WorldHelper::MovePositionByDirection(x, y, p->direction);
			WorldHelper::UpdateObjectPosition(clientId, x, y);

			UserHelper::SendMovePacket(*(*GObjectManager)[clientId], clientId);
			WorldHelper::UpdatePlayerViewList(clientId);
		}
		break;
	}
	case static_cast<char>(PacketType::CS_ATTACK):
	{
		if ((*GObjectManager)[clientId]->_state != SOCKET_STATE::ST_INGAME) break;
		if ((*GObjectManager)[clientId]->_stat.IsDead()) break;

		CS_ATTACK_PACKET* p = reinterpret_cast<CS_ATTACK_PACKET*>(packet);
		const uint32 now = WorldHelper::GetNowTime();
		if (now > (*GObjectManager)[clientId]->_lastAttackTime + 1000)
		{
			(*GObjectManager)[clientId]->_lastAttackTime = p->attack_time;
			GSector->ForEachNeighborObject(
				(*GObjectManager)[clientId]->_transform.GetSectorX(),
				(*GObjectManager)[clientId]->_transform.GetSectorY(),
				[&](uint32 id)
			{
				if ((*GObjectManager)[id]->_state != ST_INGAME) return;
				if (!IsNPC(id)) return;
				if (WorldHelper::CanAttack(clientId, id)) WorldHelper::AttackNpc(id, clientId);
			});
		}
		break;
	}
	}
}

// Legacy wrappers kept local to preserve compile-time compatibility.
void WorkerThread::AttackToNPC(uint32 npcId, uint32 playerId)
{
	WorldHelper::AttackNpc(npcId, playerId);
#if 0
	auto& npc = (*GObjectManager)[npcId];
	if (npc->_stat.IsDead()) return;

	Monster* monster  = AsMonster(npcId);
	User*    attacker = AsUser(playerId);

	const uint16 remaining = npc->_stat.TakeDamage(PLAYER_OFFENSIVE);
	if (remaining > 0)
	{
		(*GObjectManager)[playerId]->SendPlayerAtackToNPCPacket(npcId);
		return;
	}

	// NPC 사망 처리
	if (monster->GetType() == MONSTER_TYPE::AGGRO)
		if (attacker->GetTarget() == static_cast<int>(npcId))
			attacker->SetTarget(-1);

	GSector->ForEachNeighborObject(npc->_transform.GetSectorX(), npc->_transform.GetSectorY(), [&](uint32 id)
	{
		if ((*GObjectManager)[id]->_state != SOCKET_STATE::ST_INGAME) return;
		if (IsNPC(id)) return;
		if (CanSee(id, npcId)) (*GObjectManager)[id]->SendNPCDiePacket(npcId);
	});

	(*GObjectManager)[playerId]->SendNPCDiePacket(npcId);
	GSector->RemoveObject(npcId, npc->_transform.RefSectorX(), npc->_transform.RefSectorY());
	npc->ResetGameplayState();
	GTimerThread->ScheduleAfter(npcId, npc->GetTimerEpoch(), 10s, TIMER_EVENT_TYPE::EV_NPC_RESPAWN);
#endif
}

bool CanSee(uint32 from, uint32 to)
{
	return WorldHelper::CanSee(from, to);
#if 0
	if (abs((*GObjectManager)[from]->_transform.GetX() - (*GObjectManager)[to]->_transform.GetX()) >= VIEW_RANGE) return false;
	return abs((*GObjectManager)[from]->_transform.GetY() - (*GObjectManager)[to]->_transform.GetY()) <= VIEW_RANGE;
#endif
}

bool CanAttack(uint32 from, uint32 to)
{
	return WorldHelper::CanAttack(from, to);
#if 0
	if (abs((*GObjectManager)[from]->_transform.GetX() - (*GObjectManager)[to]->_transform.GetX()) >= ATTACK_RANGE) return false;
	return abs((*GObjectManager)[from]->_transform.GetY() - (*GObjectManager)[to]->_transform.GetY()) <= ATTACK_RANGE;
#endif
}
