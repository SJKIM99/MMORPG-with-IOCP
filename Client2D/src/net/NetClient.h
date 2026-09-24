#pragma once

#include "net/Shared.h"
#include "net/Socket.h"

#include <deque>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 패킷 프레이밍 + 요청 송신 도우미.
// 서버와 같은 규칙: [uint16 size][char type][본문...], 리틀엔디언, #pragma pack(1).
// ─────────────────────────────────────────────────────────────────────────────
namespace net
{
	class NetClient
	{
	public:
		// 완성된 패킷 하나(헤더 포함)를 넘겨준다.
		using PacketHandler = std::function<void(const char* packet, uint16_t size)>;

		bool Connect(const std::string& host, uint16_t port);
		void Disconnect();
		// 연결 진행 확인 + 수신/파싱/디스패치 + 송신 버퍼 비우기. 매 프레임 호출.
		void Update();

		void SetHandler(PacketHandler handler) { m_handler = std::move(handler); }

		// 테스트: 인공 지연(ms, 한쪽 방향)과 지터. 받은 패킷·보낼 패킷을 순서를 지킨 채
		// 늦게 전달해 나쁜 네트워크에서의 조작감/보정을 확인한다.
		void SetSimulatedLag(int oneWayMs, int jitterMs) { m_lagMs = oneWayMs; m_jitterMs = jitterMs; }

		[[nodiscard]] TcpSocket::State GetState() const noexcept { return m_socket.GetState(); }
		[[nodiscard]] bool IsConnected() const noexcept { return m_socket.GetState() == TcpSocket::State::Connected; }
		[[nodiscard]] const std::string& LastError() const noexcept { return m_socket.LastError(); }

		// ── 통계(디버그 오버레이) ──
		[[nodiscard]] uint64_t BytesIn() const noexcept { return m_bytesIn; }
		[[nodiscard]] uint64_t BytesOut() const noexcept { return m_bytesOut; }
		[[nodiscard]] uint32_t PacketsIn() const noexcept { return m_packetsIn; }

		// ── 요청 ──
		void SendLoginEx(const std::string& name, const std::string& password, uint8_t classId);
		void SendMove(int direction);
		void SendMoveEx(int direction, uint16_t seq);   // V2 순번 이동(예측/보정)
		void SendSkillCast(uint16_t skillId, const ObjID& target, int x, int y, uint16_t seq);
		void SendStatAlloc(uint8_t stat, uint8_t amount);
		void SendNpcTalk(uint16_t npcId);
		void SendNpcService(uint16_t npcId, uint8_t service);
		void SendAttackFacing(bool facingLeft);
		void SendTargetAttack(const ObjID& target);
		void SendSkill();
		void SendPickup();
		void SendUseItem(uint16_t slot);
		void SendEquip(uint16_t slot);
		void SendUnequip(uint16_t slot);
		void SendSwap(uint16_t a, uint16_t b);
		void SendDiscard(uint16_t slot, uint16_t count);
		void SendEnchant(uint16_t scrollSlot, uint16_t targetSlot);
		uint32_t SendShopBuy(uint16_t npcId, uint16_t itemId, uint16_t count);
		uint32_t SendShopSell(uint16_t npcId, uint16_t slot, uint16_t count);
		uint32_t SendNpcTeleport(uint16_t npcId, uint16_t destZone);
		void SendRestart();
		void SendChat(const std::string& utf8);

	private:
		template<typename Packet>
		void Queue(Packet& packet, PacketType type)
		{
			packet.size = static_cast<unsigned short>(sizeof(Packet));
			packet.type = static_cast<char>(type);
			QueueRaw(reinterpret_cast<const char*>(&packet), sizeof(Packet));
		}
		void QueueRaw(const char* raw, size_t size);
		void Flush();
		void DeliverDelayed();
		[[nodiscard]] double DelayFor();

		struct Delayed { double at; std::vector<char> bytes; };
		std::deque<Delayed> m_delayedIn, m_delayedOut;
		int               m_lagMs = 0, m_jitterMs = 0;
		double            m_lastInAt = 0.0, m_lastOutAt = 0.0;

		TcpSocket         m_socket;
		PacketHandler     m_handler;
		std::vector<char> m_recvBuf;
		size_t            m_recvLen = 0;
		std::vector<char> m_sendBuf;
		uint32_t          m_shopSeq = 0;
		uint64_t          m_bytesIn = 0;
		uint64_t          m_bytesOut = 0;
		uint32_t          m_packetsIn = 0;
	};
}
