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

		// 테스트: 나쁜 네트워크 흉내(--netsim latency=150,jitter=40,spike=2).
		//   latency  한쪽 방향 지연(ms) — 보내는 쪽 · 받는 쪽 모두
		//   jitter   패킷마다 ±ms(고르게)
		//   spike    패킷 100개 중 몇 개를 200~450ms 더 늦게(TCP 재전송 흉내)
		// TCP처럼 순서는 지킨다 — 한 패킷이 늦으면 뒤따르는 패킷도 그 뒤에 몰려 온다(머리 막힘).
		struct NetSim { int latencyMs = 0, jitterMs = 0; float spikePercent = 0.f; int spikeMinMs = 200, spikeMaxMs = 450; };
		void SetNetSim(const NetSim& s) { m_sim = s; }
		void SetSimulatedLag(int oneWayMs, int jitterMs) { m_sim.latencyMs = oneWayMs; m_sim.jitterMs = jitterMs; }
		[[nodiscard]] const NetSim& GetNetSim() const noexcept { return m_sim; }
		[[nodiscard]] bool NetSimOn() const noexcept { return m_sim.latencyMs > 0 || m_sim.jitterMs > 0 || m_sim.spikePercent > 0.f; }
		[[nodiscard]] uint32_t SpikesIn() const noexcept { return m_spikes; }
		// 흉내 지연을 재는 시계(기본: 실제 시계). 샌드박스 공유 시계로 녹화할 때는 게임 시계를 넘긴다.
		void SetTimeSource(double (*now)()) { m_timeSource = now; }

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
		// Phase 2-B: 누름 · 뗌 · 취소(SkillPhase) — 칸 · 유형은 SkillDefTable
		void SendSkillEx(uint16_t skillId, SkillPhase phase, uint8_t flags, const ObjID& target, int aimX, int aimY, uint8_t dir, uint16_t seq);
		// Phase 2-D: 기술 레벨 올림 · 내림 · 갈래 · 초기화(seq가 같으면 서버가 다시 처리하지 않는다)
		void SendSkillGrow(SkillGrowOp op, uint16_t skillId, uint8_t tier, uint8_t choice, uint16_t seq);
		void SendStatAlloc(uint8_t stat, uint8_t amount);
		void SendNpcTalk(uint16_t npcId);
		void SendNpcService(uint16_t npcId, uint8_t service);
		void SendItemSort();
		void SendQuest(QuestAction action, uint16_t questId);   // 받기 · 보고 · 포기
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
		// Phase 3-B 채널 채팅(CS_CHAT_EX): channel = ChatChannel(Normal · World · Trade · Guild · WhisperOut)
		void SendChatEx(uint8_t channel, const std::string& target, const std::string& utf8);

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
		NetSim            m_sim;
		uint32_t          m_spikes = 0;
		double          (*m_timeSource)() = nullptr;
		[[nodiscard]] double Now() const;
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
