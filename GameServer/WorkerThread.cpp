#include "pch.h"
#include "WorkerThread.h"
#include "SocketManager.h"
#include "User.h"
#include "DBThread.h"
#include "GameLogicThread.h"
#include "GameSessionManager.h"
#include "Sector.h"
#include "TimerThread.h"
#include "NPC.h"
#include "AStar.h"

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
			gListenSocket,
			GClientSocket,
			GAcceptContext._buffer,
			0,
			sizeof(SOCKADDR_IN) + 16,
			sizeof(SOCKADDR_IN) + 16,
			&bytesReceived,
			static_cast<LPOVERLAPPED>(&GAcceptContext._over));
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
			&& GClients[clientId] != nullptr
			&& GClients[clientId]->GetSessionToken() == sessionToken;
	}

	bool IsCompletionForActiveSession(uint32 clientId, const IoContext* context)
	{
		if (context == nullptr)
			return false;

		return IsActiveSession(clientId, context->_sessionToken);
	}

	void ResetAcceptSocket()
	{
		SocketManager::Close(GClientSocket);
		GClientSocket = SocketManager::CreateSocket();
	}

	void NotifyPlayerEnteredWorld(WorkerThread& worker, uint32 playerId, bool isRespawn)
	{
		const auto& player = *GClients[playerId];
		GSector->ForEachNeighborObject(player._sectorX, player._sectorY, [&](uint32 id)
		{
			auto& object = GClients[id];
			if (id == playerId)
				return;
			if (object->_state != SOCKET_STATE::ST_INGAME)
				return;
			if (CanSee(playerId, id) == false)
				return;

			if (IsPc(id))
			{
				if (isRespawn)
					object->SendRespawnPlayerPacket(playerId);
				else
					object->SendAddPlayerPacket(playerId);
			}
			else
			{
				worker.WakeUpNpc(id, playerId);
			}

			GClients[playerId]->SendAddPlayerPacket(id);
		});
	}
}

void WorkerThread::Disconnect(uint32 clientId, uint64 sessionToken)
{
	if (IsActiveSession(clientId, sessionToken) == false)
		return;

	auto& disconnectPlayer = GClients[clientId];

	GSector->ForEachNeighborObject(disconnectPlayer->_sectorX, disconnectPlayer->_sectorY, [&](uint32 id)
	{
		const auto& object = GClients[id];
		if (id == clientId)
			return;
		if (IsNPC(id))
			return;
		if (object->_state != SOCKET_STATE::ST_INGAME)
			return;
		if (CanSee(object->_id, clientId) == false)
			return;

		object->SendRemovePlayerPacket(clientId);
	});

	GSector->RemoveObject(clientId, disconnectPlayer->_sectorX, disconnectPlayer->_sectorY);
	FlushPlayerSave(clientId);

	disconnectPlayer->ResetGameplayState();
	disconnectPlayer->CloseSession();
	if (IsPc(clientId))
		GSessionManager->ReleasePlayerSessionId(clientId);
}

void WorkerThread::DoWork()
{
	while (true) {
		DWORD numOfBytes;
		ULONG_PTR key;;
		WSAOVERLAPPED* over = nullptr;

		BOOL ret = ::GetQueuedCompletionStatus(gHandle, &numOfBytes, &key, &over, INFINITE);
		IoContext* ioContext = reinterpret_cast<IoContext*>(over);

		if (ioContext == nullptr)
			continue;

		if (FALSE == ret) {
			if (ioContext->_type == IO_TYPE::IO_ACCEPT) std::cout << "Accept Error" << endl;
			else {
				std::cout << "GQCS Error on CLient[" << key << "]\n";
				const uint32 clientId = static_cast<uint32>(key);
				if (IsCompletionForActiveSession(clientId, ioContext) == false)
				{
					ReleaseIoContext(ioContext);
					continue;
				}
				const uint64 sessionToken = ioContext->_sessionToken;
				GClients[clientId]->CloseSocket();
				GGameLogicThread->Enqueue([this, clientId, sessionToken]()
				{
					Disconnect(clientId, sessionToken);
				});
				ReleaseIoContext(ioContext);
				continue;
			}
		}

		if ((0 == numOfBytes) && ((ioContext->_type == IO_TYPE::IO_RECV) || (ioContext->_type == IO_TYPE::IO_SEND))) {
			const uint32 clientId = static_cast<uint32>(key);
			if (IsCompletionForActiveSession(clientId, ioContext) == false)
			{
				ReleaseIoContext(ioContext);
				continue;
			}
			const uint64 sessionToken = ioContext->_sessionToken;
			GClients[clientId]->CloseSocket();
			GGameLogicThread->Enqueue([this, clientId, sessionToken]()
			{
				Disconnect(clientId, sessionToken);
			});
			ReleaseIoContext(ioContext);
			continue;
		}

		switch (ioContext->_type) {
		case IO_TYPE::IO_ACCEPT: {
			uint32 clientId = GetNewClientId();
			if (clientId != INVALID_CLIENT_ID) {
				User* client = GClients[clientId].get();

				if (client->AttachSocket(GClientSocket, clientId) == false)
				{
					GSessionManager->ReleasePlayerSessionId(clientId);
					ResetAcceptSocket();
					PostAcceptRequest();
					break;
				}
				const uint64 sessionToken = client->GetSessionToken();

				::setsockopt(
					GClientSocket,
					SOL_SOCKET,
					SO_UPDATE_ACCEPT_CONTEXT,
					reinterpret_cast<const char*>(&gListenSocket),
					sizeof(gListenSocket));

				::CreateIoCompletionPort(reinterpret_cast<HANDLE>(GClientSocket), gHandle, clientId, 0);

				if (client->PostRecv() == false)
				{
					client->CloseSocket();
					GGameLogicThread->Enqueue([this, clientId, sessionToken]()
					{
						Disconnect(clientId, sessionToken);
					});
				}
				else
				{
					GGameLogicThread->Enqueue([this, clientId, sessionToken]()
					{
						InitializeConnectedClient(clientId, sessionToken);
					});
				}
				GClientSocket = SocketManager::CreateSocket();
			}
			else {
				std::cout << "MAX user exceeded\n";
				ResetAcceptSocket();
			}
			PostAcceptRequest();
			break;
		}
		case IO_TYPE::IO_RECV: {
			const uint32 clientId = static_cast<uint32>(key);
			if (IsCompletionForActiveSession(clientId, ioContext) == false)
				break;

			RecvContext* recvContext = static_cast<RecvContext*>(ioContext);
			uint32 remainData = numOfBytes + GClients[clientId]->_pendingRecvBytes;
			char* p = recvContext->_buffer;
			while (remainData > 0) {

				uint32 packetSize = p[0];
				if (packetSize <= remainData) {
					const uint64 sessionToken = ioContext->_sessionToken;
					vector<char> packetCopy = CopyPacketBuffer(p, packetSize);
					GGameLogicThread->Enqueue([this, clientId, sessionToken, packet = move(packetCopy)]() mutable
					{
						if (IsActiveSession(clientId, sessionToken) == false)
							return;
						HandlePacket(clientId, packet.data());
					});
					p = p + packetSize;
					remainData = remainData - packetSize;
				}
				else break;
			}
			GClients[clientId]->_pendingRecvBytes = remainData;
			if (remainData > 0) {
				::memcpy(recvContext->_buffer, p, remainData);
			}
			if (GClients[clientId]->PostRecv() == false)
			{
				const uint64 sessionToken = ioContext->_sessionToken;
				GClients[clientId]->CloseSocket();
				GGameLogicThread->Enqueue([this, clientId, sessionToken]()
				{
					Disconnect(clientId, sessionToken);
				});
			}
			break;
		}
		case IO_TYPE::IO_SEND: {
			ReleaseIoContext(ioContext);
			break;
		}
		}
	}
}

void WorkerThread::InitializeConnectedClient(uint32 clientId, uint64 sessionToken)
{
	if (IsActiveSession(clientId, sessionToken) == false)
		return;

	User* client = GClients[clientId].get();
	client->ResetGameplayState();
	client->_maxHp = PLAYER_MAX_HP;
	client->_hp = PLAYER_MAX_HP;
	client->_offensive = PLAYER_OFFENSIVE;
	client->_die.store(false);
	client->_state = SOCKET_STATE::ST_ALLOC;
}

void WorkerThread::HandleGetPlayerInfo(uint32 clientId, uint64 sessionToken, const DB_PLAYER_INFO& playerInfo)
{
	if (IsActiveSession(clientId, sessionToken) == false)
		return;
	if (GClients[clientId]->_state != SOCKET_STATE::ST_ALLOC)
		return;

	strcpy_s(GClients[clientId]->_name, playerInfo._name.c_str());
	GClients[clientId]->_x = playerInfo._x;
	GClients[clientId]->_y = playerInfo._y;
	const bool sectorAssigned = GSector->UpdateObjectSector(clientId, GClients[clientId]->_x, GClients[clientId]->_y, GClients[clientId]->_sectorX, GClients[clientId]->_sectorY);
	ASSERT_CRASH(sectorAssigned);
	GClients[clientId]->_state = SOCKET_STATE::ST_INGAME;

	GClients[clientId]->SendLoginSuccessPacket();
	AsPlayer(clientId)->Heal();
	NotifyPlayerEnteredWorld(*this, clientId, false);
}

void WorkerThread::HandleAddPlayerInfo(uint32 clientId, uint64 sessionToken, const DB_PLAYER_INFO& playerInfo)
{
	if (IsActiveSession(clientId, sessionToken) == false)
		return;
	if (GClients[clientId]->_state != SOCKET_STATE::ST_ALLOC)
		return;

	strcpy_s(GClients[clientId]->_name, playerInfo._name.c_str());
	GClients[clientId]->_x = rand() % W_WIDTH;
	GClients[clientId]->_y = rand() % W_HEIGHT;
	const bool sectorAssigned = GSector->UpdateObjectSector(clientId, GClients[clientId]->_x, GClients[clientId]->_y, GClients[clientId]->_sectorX, GClients[clientId]->_sectorY);
	ASSERT_CRASH(sectorAssigned);
	GClients[clientId]->_state = SOCKET_STATE::ST_INGAME;

	GClients[clientId]->SendLoginSuccessPacket();
	AsPlayer(clientId)->Heal();

	DB_PLAYER_INFO addPlayer{};
	addPlayer._name = GClients[clientId]->_name;
	addPlayer._x = GClients[clientId]->_x;
	addPlayer._y = GClients[clientId]->_y;

	GDBThread->RequestAddPlayer(clientId, addPlayer);
	NotifyPlayerEnteredWorld(*this, clientId, false);
}

void WorkerThread::HandleNpcRandomMove(uint32 npcId)
{
	bool keepGoing = false;
	auto& moveNPC = GClients[npcId];
	const uint64 npcTimerEpoch = moveNPC->GetTimerEpoch();

	GSector->ForEachNeighborObject(moveNPC->_sectorX, moveNPC->_sectorY, [&](uint32 id)
	{
		if (GClients[id]->_state != ST_INGAME) return;
		if (IsNPC(id)) return;
		if (!CanSee(npcId, id)) return;
		if (!CanAttack(npcId, id)) {
			keepGoing = true;
			return;
		}

		GTimerThread->ScheduleNow(npcId, npcTimerEpoch, TIMER_EVENT_TYPE::EV_NPC_ATTACK_TO_PLAYER, id);
	});

	if (keepGoing) {
		GNPC->NPCRandomMove(npcId);
		GTimerThread->ScheduleAfter(npcId, npcTimerEpoch, 1s, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
	}
	else {
		GClients[npcId]->_active.store(false);
	}
}

void WorkerThread::HandleNpcRespawn(uint32 npcId)
{
	while (true) {
		GClients[npcId]->_x = rand() % W_WIDTH;
		GClients[npcId]->_y = rand() % W_HEIGHT;
		if (!isCollision(GClients[npcId]->_x, GClients[npcId]->_y)) {
			const bool sectorAssigned = GSector->UpdateObjectSector(npcId, GClients[npcId]->_x, GClients[npcId]->_y, GClients[npcId]->_sectorX, GClients[npcId]->_sectorY);
			ASSERT_CRASH(sectorAssigned);
			break;
		}
	}

	GClients[npcId]->_die.store(false);
	GClients[npcId]->_hp = NPC_MAX_HP;
	GClients[npcId]->_state = SOCKET_STATE::ST_INGAME;

	GSector->ForEachNeighborObject(GClients[npcId]->_sectorX, GClients[npcId]->_sectorY, [&](uint32 id)
	{
		if (GClients[id]->_state != SOCKET_STATE::ST_INGAME) return;
		if (IsNPC(id)) return;
		if (CanSee(npcId, id))
			GClients[id]->SendRespawnNPCPacket(npcId);
	});
}

void WorkerThread::HandlePlayerRespawn(uint32 playerId)
{
	while (true) {
		GClients[playerId]->_x = rand() % W_WIDTH;
		GClients[playerId]->_y = rand() % W_HEIGHT;
		if (!isCollision(GClients[playerId]->_x, GClients[playerId]->_y)) {
			const bool sectorAssigned = GSector->UpdateObjectSector(playerId, GClients[playerId]->_x, GClients[playerId]->_y, GClients[playerId]->_sectorX, GClients[playerId]->_sectorY);
			ASSERT_CRASH(sectorAssigned);
			break;
		}
	}

	GClients[playerId]->_die.store(false);
	GClients[playerId]->_hp = PLAYER_MAX_HP;
	GClients[playerId]->_state = SOCKET_STATE::ST_INGAME;

	NotifyPlayerEnteredWorld(*this, playerId, true);

	AsPlayer(playerId)->Heal();
}

void WorkerThread::HandleNpcAggroMove(uint32 npcId, uint32 aiTargetId)
{
	if (GClients[aiTargetId]->_state != ST_INGAME) {
		GClients[npcId]->_active.store(false);
		GClients[npcId]->_attack.store(false);
		return;
	}

	Monster* moveMonster = AsMonster(npcId);
	const uint64 npcTimerEpoch = moveMonster->GetTimerEpoch();
	vector<NODE>& path = moveMonster->GetPath();

	if (path.empty())
		path = FindPath(GClients[npcId]->_x, GClients[npcId]->_y, GClients[aiTargetId]->_x, GClients[aiTargetId]->_y);

	if (!path.empty()) {
		short nextX = path.back()._x;
		short nextY = path.back()._y;
		path.pop_back();
		GNPC->NPCAStarMove(npcId, nextX, nextY);
	}

	if (CanAttack(npcId, aiTargetId)) {
		if (!GClients[npcId]->_attack.load()) {
			GTimerThread->ScheduleNow(npcId, npcTimerEpoch, TIMER_EVENT_TYPE::EV_NPC_ATTACK_TO_PLAYER, aiTargetId);
		}
	}

	if (CanSee(npcId, aiTargetId)) {
		GClients[npcId]->_active.store(true);
		GTimerThread->ScheduleAfter(npcId, npcTimerEpoch, 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, aiTargetId);
	}
	else {
		GClients[npcId]->_active.store(false);
	}
}

void WorkerThread::HandleHeal(uint32 playerId)
{
	if (GClients[playerId]->_die.load())
		return;

	const uint32 currentHp = GClients[playerId]->_hp.load();
	uint16 desiredHp = static_cast<uint16>(currentHp + HEAL_SIZE);
	if (currentHp + HEAL_SIZE > PLAYER_MAX_HP)
		desiredHp = PLAYER_MAX_HP;

	GClients[playerId]->_hp.store(desiredHp);

	GClients[playerId]->SendHealPacket();

	GTimerThread->ScheduleAfter(playerId, GClients[playerId]->GetTimerEpoch(), 5s, TIMER_EVENT_TYPE::EV_HEAL);
}

void WorkerThread::HandleNpcAttackToPlayer(uint32 npcId, uint32 playerId)
{
	Player* heatedPlayer = AsPlayer(playerId);
	Monster* heatPlayer = AsMonster(npcId);

	heatPlayer->_attack.store(true);

	if (heatedPlayer->_die.load() || !CanAttack(playerId, npcId) || heatPlayer->_die.load()) {
		heatPlayer->_attack.store(false);
		return;
	}

	uint16 currentHp;
	uint16 desiredHp;

	currentHp = heatedPlayer->_hp.load();
	desiredHp = (currentHp > NPC_OFFENSIVE) ? static_cast<uint16>(currentHp - NPC_OFFENSIVE) : 0;
	heatedPlayer->_hp.store(desiredHp);
	if (desiredHp == 0) {
		heatedPlayer->_die.store(true);
	}

	if (!heatedPlayer->_die.load()) {
		const auto viewList = heatedPlayer->_viewList;
		for (auto id : viewList) {
			if (SOCKET_STATE::ST_INGAME != GClients[id]->_state) continue;
			if (!CanSee(playerId, id)) continue;
			if (IsPc(GClients[id]->_id))  GClients[id]->SendNPCAttackToPlayerPacket(heatedPlayer->_id);
		}

		if (CanAttack(npcId, playerId)) {
			GTimerThread->ScheduleAfter(npcId, heatPlayer->GetTimerEpoch(), 1s, TIMER_EVENT_TYPE::EV_NPC_ATTACK_TO_PLAYER, playerId);
		}
		else {
			heatPlayer->_attack.store(false);
		}
		return;
	}

	const auto viewList = heatedPlayer->_viewList;
	for (auto id : viewList) {
		if (SOCKET_STATE::ST_INGAME != GClients[id]->_state) continue;
		if (!CanSee(playerId, id)) continue;
		if (IsPc(GClients[id]->_id)) GClients[id]->SendPlayerDiePacket(playerId);
	}

	heatPlayer->_attack.store(false);
	heatPlayer->_active.store(false);
	GSector->RemoveObject(playerId, heatedPlayer->_sectorX, heatedPlayer->_sectorY);
	heatedPlayer->ResetGameplayState();
	GTimerThread->ScheduleAfter(heatedPlayer->_id, heatedPlayer->GetTimerEpoch(), 30s, TIMER_EVENT_TYPE::EV_PLAYER_RESPAWN);
}

bool WorkerThread::FlushPlayerSave(uint32 clientId)
{
	if (IsPc(clientId) == false)
		return false;

	DB_PLAYER_INFO playerInfo{};
	if (AsPlayer(clientId)->TryBuildSaveInfo(playerInfo) == false)
		return false;

	GDBThread->RequestSavePlayer(clientId, playerInfo);
	return true;
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
		if (GClients[clientId]->_state != SOCKET_STATE::ST_ALLOC)
			break;

		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		GDBThread->RequestLogin(clientId, GClients[clientId]->GetSessionToken(), p->name);
	}
		break;
	case static_cast<char>(PacketType::CS_MOVE):
	{
		if (GClients[clientId]->_state != SOCKET_STATE::ST_INGAME)
			break;
		if (GClients[clientId]->_die.load()) break;

		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);

		uint32 nowTime = GetNowTime();
		if (nowTime > GClients[clientId]->_lastMoveTime + 1000) {

			GClients[clientId]->_lastMoveTime = p->move_time;
			short x = GClients[clientId]->_x;
			short y = GClients[clientId]->_y;

			MovePlayer(x, y, p->direction);

			const bool sectorChanged = GSector->UpdateObjectSector(clientId, x, y, GClients[clientId]->_sectorX, GClients[clientId]->_sectorY);
			(void)sectorChanged;
			GClients[clientId]->_x = x;
			GClients[clientId]->_y = y;


			GClients[clientId]->SendMovePacket(clientId);

			/*DB_PLAYER_INFO savePlayerInfo{};
			savePlayerInfo._name = GClients[clientId]->_name;
			savePlayerInfo._x = GClients[clientId]->_x;
			savePlayerInfo._y = GClients[clientId]->_y;

			GDBThread->RequestSavePlayer(clientId, savePlayerInfo);*/

			UpdateViewList(clientId);
		}
	}
		break;
	case static_cast<char>(PacketType::CS_ATTACK):
	{
		if (GClients[clientId]->_state != SOCKET_STATE::ST_INGAME)
			break;
		if (GClients[clientId]->_die.load()) break;

		CS_ATTACK_PACKET* p = reinterpret_cast<CS_ATTACK_PACKET*>(packet);

		uint32 nowTime = GetNowTime();
		if (nowTime > GClients[clientId]->_lastMoveTime + 1000) {

			GClients[clientId]->_lastMoveTime = p->attack_time;
			auto& attackPlayer = GClients[clientId];

			GSector->ForEachNeighborObject(attackPlayer->_sectorX, attackPlayer->_sectorY, [&](uint32 id)
			{
				if (GClients[id]->_state != ST_INGAME) return;
				if (!IsNPC(id)) return;
				if (CanAttack(clientId, id)) {
					AttackToNPC(id, clientId);
				}
			});

		}
	}
		break;
	}
}

uint32 WorkerThread::GetNowTime()
{
	return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void WorkerThread::MovePlayer(short& x, short& y, char direction)
{
	switch (direction) {
	case 0: {
		if (y > 0) {
			--y;
			if (!isCollision(x, y)) y;
			else ++y;
		}
	}
		  break;
	case 1:
		if (y < W_HEIGHT - 1) {
			++y;
			if (!isCollision(x, y)) y;
			else --y;
		}
		break;
	case 2:
		if (x > 0) {
			--x;
			if (!isCollision(x, y)) x;
			else ++x;
		}
		break;
	case 3:
		if (x < W_WIDTH - 1) {
			++x;
			if (!isCollision(x, y)) x;
			else --x;
		}
		break;
	}
}

void WorkerThread::UpdateViewList(uint32 clientId)
{
	unordered_set<uint32> nearList;
	
	auto& myPlayer = GClients[clientId];
	{
		GSector->ForEachNeighborObject(myPlayer->_sectorX, myPlayer->_sectorY, [&](uint32 id)
		{
			const auto& object = GClients[id];
			if (id == clientId)
				return;
			if (object->_state != SOCKET_STATE::ST_INGAME)
				return;
			if (!CanSee(object->_id, clientId))
				return;
			nearList.insert(id);
		});

		const unordered_set<uint32> oldList = myPlayer->_viewList;

		for (auto& id : nearList) {
			auto& object = GClients[id];
			if (IsPc(id)) {
				{
					if (GClients[id]->_viewList.count(clientId)) {
						GClients[id]->SendMovePacket(clientId);
					}
					else {

						GClients[id]->SendAddPlayerPacket(clientId);
					}
				}
			}
			else {
				WakeUpNpc(id, clientId);
			}
			if (!oldList.count(id))
				GClients[clientId]->SendAddPlayerPacket(id);
		}


		for (auto& id : oldList) {
			if (!nearList.count(id)) {
				GClients[clientId]->SendRemovePlayerPacket(id);
				if (IsPc(id)) 
					GClients[id]->SendRemovePlayerPacket(clientId);
			}
		}
	}
}

void WorkerThread::WakeUpNpc(uint32 npcId, uint32 wakerId)
{
	if (GClients[npcId]->_die.load()) return;
	if (GClients[npcId]->_attack.load()) return;
	if (GClients[npcId]->_active.load()) return;

	bool expected = false;
	bool desired = true;

	Monster* npc = AsMonster(npcId);
	Player* waker = AsPlayer(wakerId);

	switch (npc->GetType()) {
	case MONSTER_TYPE::PASSIVE: {
		if (!atomic_compare_exchange_strong(&GClients[npcId]->_active, &expected, desired)) return;
		GTimerThread->ScheduleAfter(npcId, npc->GetTimerEpoch(), 1s, TIMER_EVENT_TYPE::EV_RANOM_MOVE);
		break;
	}
	case MONSTER_TYPE::AGGRO: {
		if (waker->GetTarget() != -1) break;
		if (!atomic_compare_exchange_strong(&GClients[npcId]->_active, &expected, desired)) return;
		waker->SetTarget(npcId);
		GTimerThread->ScheduleAfter(npcId, npc->GetTimerEpoch(), 1s, TIMER_EVENT_TYPE::EV_AGGRO_MOVE, wakerId);
		break;
	}
	}

}

void WorkerThread::AttackToNPC(uint32 npcId, uint32 playerId)
{
	if (GClients[npcId]->_die.load()) return;

	Monster* npc = AsMonster(npcId);
	Player* attacker = AsPlayer(playerId);

	const uint16 currentHp = GClients[npcId]->_hp.load();
	const uint16 desiredHp = (currentHp > PLAYER_OFFENSIVE) ? static_cast<uint16>(currentHp - PLAYER_OFFENSIVE) : 0;
	GClients[npcId]->_hp.store(desiredHp);

	if (desiredHp == 0) {

		if (npc->GetType() == MONSTER_TYPE::AGGRO) {
			if (attacker->GetTarget() == static_cast<int>(npcId))
				attacker->SetTarget(-1);
		}

		GSector->ForEachNeighborObject(GClients[npcId]->_sectorX, GClients[npcId]->_sectorY, [&](uint32 id)
		{
			if (GClients[id]->_state != SOCKET_STATE::ST_INGAME) return;
			if (IsNPC(id)) return;
			if (CanSee(id, npcId)) {
				GClients[id]->SendNPCDiePacket(npcId);
			}
		});

		GClients[npcId]->_die.store(true);
		GClients[playerId]->SendNPCDiePacket(npcId);

		GSector->RemoveObject(npcId, GClients[npcId]->_sectorX, GClients[npcId]->_sectorY);
		GClients[npcId]->ResetGameplayState();

		GTimerThread->ScheduleAfter(npcId, GClients[npcId]->GetTimerEpoch(), 10s, TIMER_EVENT_TYPE::EV_NPC_RESPAWN);
	}
	else {
		GClients[playerId]->SendPlayerAtackToNPCPacket(npcId);
	}
	
	
}




inline bool CanSee(uint32 from, uint32 to)
{
	if (abs(GClients[from]->_x - GClients[to]->_x) >= VIEW_RANGE) return false;
	return abs(GClients[from]->_y - GClients[to]->_y) <= VIEW_RANGE;
}

inline bool IsPc(uint32 id)
{
	return id < MAX_USER;
}

inline bool IsNPC(uint32 id)
{
	return !IsPc(id);
}

inline bool CanAttack(uint32 from, uint32 to)
{
	if (abs(GClients[from]->_x - GClients[to]->_x) >= ATTACK_RANGE) return false;
	return abs(GClients[from]->_y - GClients[to]->_y) <= ATTACK_RANGE;
}
