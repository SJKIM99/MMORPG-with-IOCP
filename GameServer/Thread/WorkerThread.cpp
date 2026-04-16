#include "pch.h"
#include "WorkerThread.h"
#include "SocketManager.h"
#include "User.h"
#include "Monster.h"
#include "DBThread.h"
#include "GameLogicThread.h"
#include "GameSessionManager.h"
#include "GameSession.h"
#include "Sector.h"
#include "TimerThread.h"
#include "AStar.h"
#include "UserHelper.h"
#include "SubjectHelper.h"
#include "MonsterHelper.h"
#include "Route.h"

namespace
{
	void PostAcceptRequest()
	{
		GAcceptContext.ResetOverlapped();
		DWORD bytesReceived = 0;
		SocketManager::AcceptEx(
			gListenSocket, GClientSocket,
			GAcceptContext.m_buffer, 0,
			sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
			&bytesReceived, static_cast<LPOVERLAPPED>(&GAcceptContext.m_over));
	}

	vector<char> CopyPacketBuffer(const char* packet, uint32_t packetSize)
	{
		vector<char> buffer(packetSize);
		::memcpy(buffer.data(), packet, packetSize);
		return buffer;
	}

	void EnqueueDisconnect(const shared_ptr<GameSession>& session)
	{
		session->CloseSocket();

		if (auto player = session->GetOwner())
		{
			const ObjID objId = player->GetObjID();
			GGameLogicThread->Enqueue([objId]() { GWorkerThread->Disconnect(objId); });
		}
		else
		{
			// 로그인 전 접속 끊김 - User 없이 세션만 정리
			GGameLogicThread->Enqueue([session]()
			{
				session->CloseSession();
				GSessionManager->ReleaseSession(session.get());
			});
		}
	}

	void ResetAcceptSocket()
	{
		SocketManager::Close(GClientSocket);
		GClientSocket = SocketManager::CreateSocket();
	}
}

void WorkerThread::Disconnect(ObjID clientId)
{
	auto user = GGameObjectManager->Seek<User>(clientId);
	if (user == nullptr)
		return;

	GSector->ForEachNeighborObject(user->GetSectorX(), user->GetSectorY(), [&](const shared_ptr<Subject>& subject)
	{
		const auto subjectId = subject->GetObjID();
		if (subjectId == clientId) return;
		if (subjectId.GetCategory<EnumCategory>() != EnumCategory::eUser) return;

		auto neighbor = static_pointer_cast<User>(subject);
		auto session  = neighbor->GetGameSession();
		if (session == nullptr || session->m_state != SOCKET_STATE::ST_INGAME) return;
		if (!SubjectHelper::CanSee(subjectId, clientId)) return;

		UserHelper::SendRemovePlayerPacket(neighbor, clientId);
	});

	GSector->RemoveObject(clientId, user->RefSectorX(), user->RefSectorY());
	(void)UserHelper::FlushPlayerSave(clientId);

	if (auto session = user->GetGameSession())
	{
		session->CloseSession();
		GSessionManager->ReleaseSession(session.get());
	}

	(void)GGameObjectManager->Delete(clientId);
}

void WorkerThread::DoWork()
{
	while (true)
	{
		DWORD numOfBytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over = nullptr;

		BOOL ret = ::GetQueuedCompletionStatus(gHandle, &numOfBytes, &key, &over, INFINITE);
		auto ownedIoContext = TakePendingIoContext(reinterpret_cast<IoContext*>(over));
		IoContext* ioContext = ownedIoContext != nullptr ? ownedIoContext.get() : reinterpret_cast<IoContext*>(over);

		if (ioContext == nullptr)
			continue;

		auto* sessionPtr = reinterpret_cast<GameSession*>(key);

		if (FALSE == ret)
		{
			if (ioContext->m_type == IO_TYPE::IO_ACCEPT)
			{
				std::cout << "Accept Error\n";
			}
			else
			{
				if (auto session = GSessionManager->FindSession(sessionPtr))
					EnqueueDisconnect(session);
				continue;
			}
		}

		if (numOfBytes == 0
			&& (ioContext->m_type == IO_TYPE::IO_RECV || ioContext->m_type == IO_TYPE::IO_SEND))
		{
			if (auto session = GSessionManager->FindSession(sessionPtr))
				EnqueueDisconnect(session);
			continue;
		}

		switch (ioContext->m_type)
		{
		case IO_TYPE::IO_ACCEPT:
		{
			auto session = GSessionManager->CreateSession();
			if (session != nullptr)
			{
				if (session->AttachSocket(GClientSocket) == false)
				{
					GSessionManager->ReleaseSession(session.get());
					ResetAcceptSocket();
					PostAcceptRequest();
					break;
				}

				::setsockopt(GClientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
					reinterpret_cast<const char*>(&gListenSocket), sizeof(gListenSocket));
				::CreateIoCompletionPort(reinterpret_cast<HANDLE>(GClientSocket), gHandle,
					reinterpret_cast<ULONG_PTR>(session.get()), 0);

				session->m_state = SOCKET_STATE::ST_ALLOC;

				if (session->PostRecv() == false)
					EnqueueDisconnect(session);

				GClientSocket = SocketManager::CreateSocket();
			}
			else
			{
				std::cout << "No available player object key exhausted\n";
				ResetAcceptSocket();
			}
			PostAcceptRequest();
			break;
		}
		case IO_TYPE::IO_RECV:
		{
			auto session = GSessionManager->FindSession(sessionPtr);
			if (session == nullptr) break;

			auto recvContext = static_pointer_cast<RecvContext>(ownedIoContext);
			if (recvContext == nullptr) break;
			uint32_t remainData = numOfBytes + session->m_pendingRecvBytes;
			char* p = recvContext->m_buffer;

			while (remainData > 0)
			{
				if (remainData < sizeof(uint16_t)) break;
				const uint16_t packetSize = *reinterpret_cast<const uint16_t*>(p);
				if (packetSize == 0) break;
				if (packetSize > remainData) break;

				vector<char> copy = CopyPacketBuffer(p, packetSize);
				GGameLogicThread->Enqueue([session, packet = move(copy)]() mutable
				{
					Route::Dispatch(session, packet.data());
				});
				p += packetSize;
				remainData -= packetSize;
			}

			session->m_pendingRecvBytes = remainData;
			if (remainData > 0)
				::memcpy(recvContext->m_buffer, p, remainData);

			if (session->PostRecv() == false)
				EnqueueDisconnect(session);
			break;
		}
		case IO_TYPE::IO_SEND:
			break;
		}
	}
}
