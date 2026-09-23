#include "net/Socket.h"

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	using socklen_t = int;
	#define CLOSESOCK closesocket
	#define SOCK_ERRNO WSAGetLastError()
	#define ERR_WOULDBLOCK WSAEWOULDBLOCK
	#define ERR_INPROGRESS WSAEWOULDBLOCK
#else
	#include <sys/socket.h>
	#include <sys/select.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <fcntl.h>
	#include <unistd.h>
	#include <cerrno>
	#define CLOSESOCK ::close
	#define SOCK_ERRNO errno
	#define ERR_WOULDBLOCK EWOULDBLOCK
	#define ERR_INPROGRESS EINPROGRESS
#endif

#include <cstring>

namespace
{
#ifdef _WIN32
	struct WinsockInit
	{
		WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
		~WinsockInit() { WSACleanup(); }
	};
	WinsockInit& EnsureWinsock() { static WinsockInit init; return init; }
	using SockHandle = SOCKET;
	constexpr SockHandle kInvalid = INVALID_SOCKET;
#else
	using SockHandle = int;
	constexpr SockHandle kInvalid = -1;
#endif

	SockHandle H(intptr_t s) { return static_cast<SockHandle>(s); }

	bool SetNonBlocking(SockHandle s)
	{
#ifdef _WIN32
		u_long mode = 1;
		return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
		const int flags = fcntl(s, F_GETFL, 0);
		return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
	}

	bool IsWouldBlock(int err)
	{
#ifdef _WIN32
		return err == WSAEWOULDBLOCK;
#else
		return err == EWOULDBLOCK || err == EAGAIN;
#endif
	}
}

namespace net
{
	TcpSocket::~TcpSocket()
	{
		Close();
	}

	bool TcpSocket::StartConnect(const std::string& host, uint16_t port)
	{
#ifdef _WIN32
		EnsureWinsock();
#endif
		Close();
		m_error.clear();

		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		addrinfo* result = nullptr;
		const std::string portStr = std::to_string(port);
		if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0 || result == nullptr)
		{
			m_error = "주소를 찾을 수 없습니다: " + host;
			m_state = State::Failed;
			return false;
		}

		SockHandle s = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (s == kInvalid)
		{
			freeaddrinfo(result);
			m_error = "소켓 생성 실패";
			m_state = State::Failed;
			return false;
		}

		// 서버와 같은 설정: 작은 패킷(이동/공격)을 모으지 않고 즉시 보낸다.
		int noDelay = 1;
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
		SetNonBlocking(s);

		const int rc = ::connect(s, result->ai_addr, static_cast<socklen_t>(result->ai_addrlen));
		freeaddrinfo(result);

		m_sock = static_cast<intptr_t>(s);
		if (rc == 0)
		{
			m_state = State::Connected;
			return true;
		}

		const int err = SOCK_ERRNO;
#ifdef _WIN32
		if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS)
#else
		if (err == EINPROGRESS || err == EWOULDBLOCK)
#endif
		{
			m_state = State::Connecting;
			return true;
		}

		m_error = "연결 실패(" + std::to_string(err) + ")";
		Close();
		m_state = State::Failed;
		return false;
	}

	TcpSocket::State TcpSocket::Poll()
	{
		if (m_state != State::Connecting)
			return m_state;

		fd_set writeSet, errorSet;
		FD_ZERO(&writeSet);
		FD_ZERO(&errorSet);
		FD_SET(H(m_sock), &writeSet);
		FD_SET(H(m_sock), &errorSet);
		timeval tv{ 0, 0 };
		const int n = ::select(static_cast<int>(m_sock) + 1, nullptr, &writeSet, &errorSet, &tv);
		if (n < 0)
		{
			m_error = "select 실패";
			Close();
			m_state = State::Failed;
			return m_state;
		}
		if (n == 0)
			return m_state;  // 아직 연결 중

		int soError = 0;
		socklen_t len = sizeof(soError);
		getsockopt(H(m_sock), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
		if (FD_ISSET(H(m_sock), &errorSet) || soError != 0)
		{
			m_error = "서버에 연결할 수 없습니다(" + std::to_string(soError) + ")";
			Close();
			m_state = State::Failed;
			return m_state;
		}

		m_state = State::Connected;
		return m_state;
	}

	int TcpSocket::Send(const void* data, int len)
	{
		if (m_state != State::Connected || len <= 0)
			return m_state == State::Connected ? 0 : -1;

		const int n = static_cast<int>(::send(H(m_sock), static_cast<const char*>(data), len, 0));
		if (n >= 0)
			return n;
		if (IsWouldBlock(SOCK_ERRNO))
			return 0;
		m_error = "송신 오류";
		Close();
		m_state = State::Failed;
		return -1;
	}

	int TcpSocket::Recv(void* buf, int len)
	{
		if (m_state != State::Connected)
			return -1;

		const int n = static_cast<int>(::recv(H(m_sock), static_cast<char*>(buf), len, 0));
		if (n > 0)
			return n;
		if (n == 0)
		{
			m_error = "서버와의 연결이 끊어졌습니다";
			Close();
			m_state = State::Failed;
			return -1;
		}
		if (IsWouldBlock(SOCK_ERRNO))
			return 0;
		m_error = "수신 오류";
		Close();
		m_state = State::Failed;
		return -1;
	}

	void TcpSocket::Close()
	{
		if (m_sock != -1)
		{
			CLOSESOCK(H(m_sock));
			m_sock = -1;
		}
		if (m_state != State::Failed)
			m_state = State::Closed;
	}
}
