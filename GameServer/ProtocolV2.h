#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 프로토콜 V2 — Dark Fantasy Storybook 고도화(이동·직업·스킬·아이템·퀘스트·길드).
//
// Protocol.h 끝에서 include된다(PacketType 값은 Protocol.h의 enum 끝에 이어 붙인다).
// V2 패킷은 확장 세션(USER_LOGIN_EX_REQ로 접속)에만 오간다. 레거시 봇/클라이언트는
// 이 헤더의 어떤 패킷도 보내거나 받지 않는다.
//
// 레거시 필드 재사용: SUBJECT_MOVE_NFY.move_time(예전엔 항상 0)에 **그 걸음에 걸린
// 시간(ms)**을 싣는다. 0이면 순간이동/재배치라 보간 없이 바로 옮기면 된다. 봇은 이
// 필드를 읽지 않는다.
// ─────────────────────────────────────────────────────────────────────────────

#pragma pack (push, 1)

// ── 이동 ─────────────────────────────────────────────────────────────────────
// dir: USER_MOVE_REQ와 같은 8방향(0:UP 1:DOWN 2:LEFT 3:RIGHT 4:UP-LEFT 5:UP-RIGHT
// 6:DOWN-LEFT 7:DOWN-RIGHT). seq: 클라이언트가 이동마다 1씩 올리는 번호(16비트 순환).
struct USER_MOVE_EX_REQ_PACKET
{
	unsigned short size;
	char           type;
	uint8_t        dir;
	uint16_t       seq;
};

enum class MoveResult : uint8_t
{
	Ok,          // 이동함(벽에 막혀 제자리여도 규칙대로 처리됐으면 Ok)
	TooFast,     // 토큰 버킷 초과 — 서버 좌표로 되돌려야 한다
	Busy,        // 사망/존 이동 중 등 지금은 움직일 수 없음
	Stunned,     // 기절·빙결 등 이동 불가 상태
};

// 확장 세션의 모든 USER_MOVE_EX_REQ에 정확히 하나씩, 받은 순서대로 응답한다.
// x/y는 처리 직후의 서버 좌표(거절이면 현재 좌표) — 클라이언트는 이 값으로 예측을 맞춘다.
struct USER_MOVE_EX_ACK_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       seq;
	short          x, y;
	uint8_t        result;    // MoveResult
	uint16_t       stepMs;    // 이 걸음의 비용(ms). 클라 예측 속도 보정용
};

// 로그인 직후 한 번. 클라이언트가 추측하지 말고 서버 값을 쓰도록.
struct SERVER_CONFIG_INF_PACKET
{
	unsigned short size;
	char           type;
	uint16_t       protocolVersion;   // PROTOCOL_EX_VERSION
	uint8_t        viewRange;         // 시야(체비셰프 칸)
	uint16_t       baseStepMs;        // 이동속도 100%일 때 직선 한 칸 시간
	uint16_t       diagPermille;      // 대각선 비용 배율(‰)
	uint16_t       moveSpeedPercent;  // 지금 내 이동속도(%)
};

#pragma pack (pop)

// ── Client → Server 최대 크기(수신 버퍼) ─────────────────────────────────────
namespace ClientPacketSizeDetailV2
{
	constexpr size_t v01 = ProtocolConstMaxSize(MAX_CLIENT_PACKET_SIZE_V1, sizeof(USER_MOVE_EX_REQ_PACKET));
}
constexpr size_t MAX_CLIENT_PACKET_SIZE = ClientPacketSizeDetailV2::v01;
