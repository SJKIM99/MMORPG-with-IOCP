#include "net/NetClient.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>

// 서버 헤더의 정적 멤버 정의(서버에서는 ObjID.cpp/ContentiD.cpp에 있다)
ContentID ContentID::npos{};
ObjID ObjID::npos{};

namespace net
{
	namespace
	{
		constexpr size_t kRecvBufferSize = 64 * 1024;
		constexpr uint16_t kMaxPacketSize = 4096;   // 서버 SEND_BATCH_BUFFER_SIZE와 같은 상한

		double NowSec()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		void CopyFixed(char* dst, size_t dstSize, const std::string& src)
		{
			std::memset(dst, 0, dstSize);
			std::memcpy(dst, src.data(), std::min(src.size(), dstSize));
		}
	}

	bool NetClient::Connect(const std::string& host, uint16_t port)
	{
		m_recvBuf.assign(kRecvBufferSize, 0);
		m_recvLen = 0;
		m_sendBuf.clear();
		return m_socket.StartConnect(host, port);
	}

	void NetClient::Disconnect()
	{
		m_socket.Close();
		m_recvLen = 0;
		m_sendBuf.clear();
		m_delayedIn.clear();
		m_delayedOut.clear();
	}

	double NetClient::DelayFor()
	{
		const int j = m_jitterMs > 0 ? (std::rand() % (m_jitterMs * 2 + 1)) - m_jitterMs : 0;
		return std::max(0, m_lagMs + j) / 1000.0;
	}

	void NetClient::QueueRaw(const char* raw, size_t size)
	{
		if (m_lagMs > 0)
		{
			const double at = std::max(m_lastOutAt, NowSec() + DelayFor());
			m_lastOutAt = at;
			m_delayedOut.push_back(Delayed{ at, std::vector<char>(raw, raw + size) });
			return;
		}
		m_sendBuf.insert(m_sendBuf.end(), raw, raw + size);
		Flush();
	}

	void NetClient::DeliverDelayed()
	{
		const double now = NowSec();
		while (!m_delayedOut.empty() && m_delayedOut.front().at <= now)
		{
			const auto& d = m_delayedOut.front().bytes;
			m_sendBuf.insert(m_sendBuf.end(), d.begin(), d.end());
			m_delayedOut.pop_front();
		}
		while (!m_delayedIn.empty() && m_delayedIn.front().at <= now)
		{
			const std::vector<char> d = std::move(m_delayedIn.front().bytes);
			m_delayedIn.pop_front();
			if (m_handler)
				m_handler(d.data(), static_cast<uint16_t>(d.size()));
			if (!IsConnected())
				return;
		}
	}

	void NetClient::Update()
	{
		if (m_socket.Poll() != TcpSocket::State::Connected)
			return;

		DeliverDelayed();
		if (!IsConnected())
			return;
		Flush();

		// 받을 수 있는 만큼 받는다
		while (true)
		{
			if (m_recvLen >= m_recvBuf.size())
				m_recvBuf.resize(m_recvBuf.size() * 2);
			const int n = m_socket.Recv(m_recvBuf.data() + m_recvLen, static_cast<int>(m_recvBuf.size() - m_recvLen));
			if (n <= 0)
				break;
			m_recvLen += static_cast<size_t>(n);
			m_bytesIn += static_cast<uint64_t>(n);
		}

		// 완성된 패킷만 잘라 처리하고, 남은 조각은 앞으로 당긴다
		size_t offset = 0;
		while (m_recvLen - offset >= 3)
		{
			uint16_t size = 0;
			std::memcpy(&size, m_recvBuf.data() + offset, sizeof(size));
			if (size < 3 || size > kMaxPacketSize)
			{
				// 프레이밍이 깨졌다 — 계속 읽으면 이후 모든 패킷이 어긋나므로 끊는다.
				Disconnect();
				return;
			}
			if (m_recvLen - offset < size)
				break;
			++m_packetsIn;
			if (m_lagMs > 0)
			{
				// 테스트 지연: 순서를 지키며(앞 패킷보다 먼저 오지 않게) 늦게 전달
				const double at = std::max(m_lastInAt, NowSec() + DelayFor());
				m_lastInAt = at;
				m_delayedIn.push_back(Delayed{ at, std::vector<char>(m_recvBuf.data() + offset, m_recvBuf.data() + offset + size) });
			}
			else if (m_handler)
				m_handler(m_recvBuf.data() + offset, size);
			offset += size;
			if (!IsConnected())
				return;  // 핸들러 안에서 끊었을 수 있다
		}
		if (offset > 0)
		{
			std::memmove(m_recvBuf.data(), m_recvBuf.data() + offset, m_recvLen - offset);
			m_recvLen -= offset;
		}
	}

	void NetClient::Flush()
	{
		if (m_sendBuf.empty() || !IsConnected())
			return;
		const int n = m_socket.Send(m_sendBuf.data(), static_cast<int>(m_sendBuf.size()));
		if (n > 0)
		{
			m_bytesOut += static_cast<uint64_t>(n);
			m_sendBuf.erase(m_sendBuf.begin(), m_sendBuf.begin() + n);
		}
	}

	void NetClient::SendLoginEx(const std::string& name, const std::string& password, uint8_t classId)
	{
		USER_LOGIN_EX_REQ_PACKET p{};
		CopyFixed(p.name, NAME_SIZE, name);
		CopyFixed(p.password, PASSWORD_SIZE, password);
		p.version = PROTOCOL_EX_VERSION;
		p.classId = classId;
		Queue(p, PacketType::USER_LOGIN_EX_REQ);
	}

	void NetClient::SendMove(int direction)
	{
		USER_MOVE_REQ_PACKET p{};
		p.direction = static_cast<char>(direction);
		p.move_time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		Queue(p, PacketType::USER_MOVE_REQ);
	}

	void NetClient::SendMoveEx(int direction, uint16_t seq)
	{
		USER_MOVE_EX_REQ_PACKET p{};
		p.dir = static_cast<uint8_t>(direction);
		p.seq = seq;
		Queue(p, PacketType::USER_MOVE_EX_REQ);
	}

	void NetClient::SendSkillCast(uint16_t skillId, const ObjID& target, int x, int y, uint16_t seq)
	{
		SKILL_CAST_REQ_PACKET p{};
		p.skillId = skillId;
		p.targetId = target;
		p.x = static_cast<short>(x);
		p.y = static_cast<short>(y);
		p.seq = seq;
		Queue(p, PacketType::SKILL_CAST_REQ);
	}

	void NetClient::SendStatAlloc(uint8_t stat, uint8_t amount)
	{
		STAT_ALLOC_REQ_PACKET p{};
		p.stat = stat;
		p.amount = amount;
		Queue(p, PacketType::STAT_ALLOC_REQ);
	}

	void NetClient::SendNpcTalk(uint16_t npcId)
	{
		NPC_TALK_REQ_PACKET p{};
		p.npcId = npcId;
		Queue(p, PacketType::NPC_TALK_REQ);
	}

	void NetClient::SendNpcService(uint16_t npcId, uint8_t service)
	{
		NPC_SERVICE_REQ_PACKET p{};
		p.npcId = npcId;
		p.service = service;
		Queue(p, PacketType::NPC_SERVICE_REQ);
	}

	void NetClient::SendItemSort()
	{
		ITEM_SORT_REQ_PACKET p{};
		p.mode = static_cast<uint8_t>(SortMode::Default);
		Queue(p, PacketType::ITEM_SORT_REQ);
	}

	void NetClient::SendAttackFacing(bool facingLeft)
	{
		USER_ATTACK_REQ_PACKET p{};
		p.facing = facingLeft ? 1 : 0;
		Queue(p, PacketType::USER_ATTACK_REQ);
	}

	void NetClient::SendTargetAttack(const ObjID& target)
	{
		USER_TARGET_ATTACK_REQ_PACKET p{};
		p.targetId = target;
		Queue(p, PacketType::USER_TARGET_ATTACK_REQ);
	}

	void NetClient::SendSkill()
	{
		USER_SKILL_REQ_PACKET p{};
		Queue(p, PacketType::USER_SKILL_REQ);
	}

	void NetClient::SendPickup()
	{
		ITEM_PICKUP_REQ_PACKET p{};
		Queue(p, PacketType::ITEM_PICKUP_REQ);
	}

	void NetClient::SendUseItem(uint16_t slot)
	{
		ITEM_USE_REQ_PACKET p{};
		p.slotIndex = slot;
		Queue(p, PacketType::ITEM_USE_REQ);
	}

	void NetClient::SendEquip(uint16_t slot)
	{
		ITEM_EQUIP_REQ_PACKET p{};
		p.slotIndex = slot;
		Queue(p, PacketType::ITEM_EQUIP_REQ);
	}

	void NetClient::SendUnequip(uint16_t slot)
	{
		ITEM_UNEQUIP_REQ_PACKET p{};
		p.slotIndex = slot;
		Queue(p, PacketType::ITEM_UNEQUIP_REQ);
	}

	void NetClient::SendSwap(uint16_t a, uint16_t b)
	{
		ITEM_SWAP_REQ_PACKET p{};
		p.slotIndexA = a;
		p.slotIndexB = b;
		Queue(p, PacketType::ITEM_SWAP_REQ);
	}

	void NetClient::SendDiscard(uint16_t slot, uint16_t count)
	{
		ITEM_DISCARD_REQ_PACKET p{};
		p.slotIndex = slot;
		p.count = count;
		Queue(p, PacketType::ITEM_DISCARD_REQ);
	}

	void NetClient::SendEnchant(uint16_t scrollSlot, uint16_t targetSlot)
	{
		ITEM_ENCHANT_REQ_PACKET p{};
		p.scrollSlot = scrollSlot;
		p.targetSlot = targetSlot;
		Queue(p, PacketType::ITEM_ENCHANT_REQ);
	}

	uint32_t NetClient::SendShopBuy(uint16_t npcId, uint16_t itemId, uint16_t count)
	{
		SHOP_BUY_REQ_PACKET p{};
		p.npcId = npcId;
		p.itemId = itemId;
		p.count = count;
		p.seq = ++m_shopSeq;
		Queue(p, PacketType::SHOP_BUY_REQ);
		return p.seq;
	}

	uint32_t NetClient::SendShopSell(uint16_t npcId, uint16_t slot, uint16_t count)
	{
		SHOP_SELL_REQ_PACKET p{};
		p.npcId = npcId;
		p.slotIndex = slot;
		p.count = count;
		p.seq = ++m_shopSeq;
		Queue(p, PacketType::SHOP_SELL_REQ);
		return p.seq;
	}

	uint32_t NetClient::SendNpcTeleport(uint16_t npcId, uint16_t destZone)
	{
		NPC_TELEPORT_REQ_PACKET p{};
		p.npcId = npcId;
		p.destIndex = destZone;
		p.seq = ++m_shopSeq;
		Queue(p, PacketType::NPC_TELEPORT_REQ);
		return p.seq;
	}

	void NetClient::SendRestart()
	{
		USER_RESTART_REQ_PACKET p{};
		Queue(p, PacketType::USER_RESTART_REQ);
	}

	void NetClient::SendChat(const std::string& utf8)
	{
		CS_CHAT_PACKET p{};
		// 서버는 CHAT_SIZE 바이트를 그대로 복사한다. UTF-8 글자 중간에서 잘리지 않도록
		// 마지막 완성 글자 경계까지만 넣는다(널 종료 1바이트 확보).
		size_t len = std::min(utf8.size(), static_cast<size_t>(CHAT_SIZE - 1));
		while (len > 0 && len < utf8.size() && (static_cast<unsigned char>(utf8[len]) & 0xC0) == 0x80)
			--len;
		std::memcpy(p.mess, utf8.data(), len);
		Queue(p, PacketType::CS_CHAT);
	}
}
