#pragma once

#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 논블로킹 TCP 소켓(윈속/BSD 공용). 이 헤더는 raylib·windows.h 어느 쪽도 끌어오지
// 않는다 — raylib과 windows.h의 이름 충돌(Rectangle, DrawText, CloseWindow...)을
// 피하기 위해 플랫폼 헤더는 Socket.cpp 안에만 둔다.
// ─────────────────────────────────────────────────────────────────────────────
namespace net
{
	class TcpSocket
	{
	public:
		enum class State { Closed, Connecting, Connected, Failed };

		TcpSocket() = default;
		~TcpSocket();
		TcpSocket(const TcpSocket&) = delete;
		TcpSocket& operator=(const TcpSocket&) = delete;

		// 논블로킹 연결 시작. 즉시 실패하면 false(LastError 참고).
		bool StartConnect(const std::string& host, uint16_t port);
		// 연결 진행 상황 확인(매 프레임 호출).
		State Poll();

		// 보낸 바이트 수, 버퍼가 차서 못 보냈으면 0, 오류면 -1
		int Send(const void* data, int len);
		// 받은 바이트 수, 아직 없으면 0, 끊김/오류면 -1
		int Recv(void* buf, int len);

		void Close();

		[[nodiscard]] State GetState() const noexcept { return m_state; }
		[[nodiscard]] const std::string& LastError() const noexcept { return m_error; }

	private:
		intptr_t    m_sock  = -1;
		State       m_state = State::Closed;
		std::string m_error;
	};
}
