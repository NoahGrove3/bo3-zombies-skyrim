// UDP side of the co-op tunnel (no precompiled header: winsock must come before windows.h).
// The friend's Skyrim Together connects to 127.0.0.1; their packets ride Steam (Net.cpp) to the host's game, which
// hands them to the host's Skyrim Together server on 127.0.0.1 and sends the answers back the same way.
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <map>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

namespace bo3z::Relay
{
	namespace
	{
		bool        g_wsa = false;
		SOCKET      g_listen = INVALID_SOCKET;  // client: where the local Skyrim Together sends
		sockaddr_in g_app{};                    // client: the local Skyrim Together's address (last sender)
		bool        g_haveApp = false;
		std::map<std::uint64_t, SOCKET> g_peers;  // host: one socket per friend, talking to the local server
		std::mutex  g_lock;

		bool Wsa()
		{
			if (!g_wsa) {
				WSADATA d{};
				g_wsa = WSAStartup(MAKEWORD(2, 2), &d) == 0;
			}
			return g_wsa;
		}

		SOCKET Udp(std::uint16_t a_port)
		{
			SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (s == INVALID_SOCKET) {
				return s;
			}
			u_long nb = 1;
			::ioctlsocket(s, FIONBIO, &nb);
			sockaddr_in a{};
			a.sin_family = AF_INET;
			a.sin_port = htons(a_port);
			a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
				::closesocket(s);
				return INVALID_SOCKET;
			}
			return s;
		}
	}

	// Is anything (a Skyrim Together server) already using this local UDP port?
	bool PortInUse(std::uint16_t a_port)
	{
		if (!Wsa()) {
			return false;
		}
		SOCKET s = Udp(a_port);
		if (s == INVALID_SOCKET) {
			return true;
		}
		::closesocket(s);
		return false;
	}

	bool OpenClient(std::uint16_t a_port)
	{
		std::scoped_lock l(g_lock);
		if (g_listen != INVALID_SOCKET) {
			return true;
		}
		if (!Wsa()) {
			return false;
		}
		g_listen = Udp(a_port);
		g_haveApp = false;
		return g_listen != INVALID_SOCKET;
	}

	int RecvFromApp(std::uint8_t* a_buf, int a_cap)
	{
		std::scoped_lock l(g_lock);
		if (g_listen == INVALID_SOCKET) {
			return -1;
		}
		sockaddr_in from{};
		int         fl = sizeof(from);
		const int   n = ::recvfrom(g_listen, reinterpret_cast<char*>(a_buf), a_cap, 0, reinterpret_cast<sockaddr*>(&from), &fl);
		if (n > 0) {
			g_app = from;
			g_haveApp = true;
		}
		return n;
	}

	void SendToApp(const std::uint8_t* a_data, int a_size)
	{
		std::scoped_lock l(g_lock);
		if (g_listen != INVALID_SOCKET && g_haveApp) {
			::sendto(g_listen, reinterpret_cast<const char*>(a_data), a_size, 0, reinterpret_cast<const sockaddr*>(&g_app), sizeof(g_app));
		}
	}

	void SendToServer(std::uint64_t a_peer, std::uint16_t a_port, const std::uint8_t* a_data, int a_size)
	{
		std::scoped_lock l(g_lock);
		if (!Wsa()) {
			return;
		}
		auto it = g_peers.find(a_peer);
		if (it == g_peers.end()) {
			SOCKET s = Udp(0);  // any free port: the server sees each friend as its own address
			if (s == INVALID_SOCKET) {
				return;
			}
			it = g_peers.emplace(a_peer, s).first;
		}
		sockaddr_in to{};
		to.sin_family = AF_INET;
		to.sin_port = htons(a_port);
		to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		::sendto(it->second, reinterpret_cast<const char*>(a_data), a_size, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
	}

	// Host: the server's answers, friend by friend. Returns the friend whose socket had data (0 none).
	std::uint64_t RecvFromServer(std::uint8_t* a_buf, int a_cap, int& a_size)
	{
		std::scoped_lock l(g_lock);
		for (auto& [peer, s] : g_peers) {
			const int n = ::recv(s, reinterpret_cast<char*>(a_buf), a_cap, 0);
			if (n > 0) {
				a_size = n;
				return peer;
			}
		}
		a_size = 0;
		return 0;
	}

	void CloseAll()
	{
		std::scoped_lock l(g_lock);
		if (g_listen != INVALID_SOCKET) {
			::closesocket(g_listen);
			g_listen = INVALID_SOCKET;
		}
		for (auto& [p, s] : g_peers) {
			::closesocket(s);
		}
		g_peers.clear();
		g_haveApp = false;
	}
}
