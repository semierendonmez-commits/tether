// A UDP socket bound to a port the OS picks, and a host resolver (norns.local through mDNS on
// macOS, through avahi / nss-mdns on Linux). Blocking calls: only the link thread uses them. No JUCE.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using tether_socket_t = SOCKET;
  #define TETHER_BAD_SOCKET INVALID_SOCKET
#else
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <poll.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using tether_socket_t = int;
  #define TETHER_BAD_SOCKET (-1)
#endif

namespace tether
{

struct Address
{
    sockaddr_in sa {};
    bool valid = false;
    std::string text() const
    {
        if (! valid) return {};
        char b[64] {};
        inet_ntop (AF_INET, &sa.sin_addr, b, sizeof (b));
        return std::string (b) + ":" + std::to_string (ntohs (sa.sin_port));
    }
    std::string ip() const
    {
        if (! valid) return {};
        char b[64] {};
        inet_ntop (AF_INET, &sa.sin_addr, b, sizeof (b));
        return b;
    }
    bool sameHost (const sockaddr_in& o) const noexcept { return valid && o.sin_addr.s_addr == sa.sin_addr.s_addr; }
};

class Udp
{
public:
    Udp() = default;
    ~Udp() { close(); }
    Udp (const Udp&) = delete;
    Udp& operator= (const Udp&) = delete;

    // port 0: any free port (read it back with port())
    bool open (uint16_t port = 0, bool loopbackOnly = false)
    {
        close();
#if defined(_WIN32)
        WSADATA w;
        if (WSAStartup (MAKEWORD (2, 2), &w) != 0) return false;
        wsa = true;
#endif
        fd = ::socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd == TETHER_BAD_SOCKET) return false;
        int big = 1 << 20;
        ::setsockopt (fd, SOL_SOCKET, SO_RCVBUF, (const char*) &big, sizeof (big));
        sockaddr_in a {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl (loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
        a.sin_port = htons (port);
        if (::bind (fd, (sockaddr*) &a, sizeof (a)) != 0) { close(); return false; }
        socklen_t len = sizeof (a);
        ::getsockname (fd, (sockaddr*) &a, &len);
        bound = ntohs (a.sin_port);
        return true;
    }

    void close()
    {
        if (fd != TETHER_BAD_SOCKET)
        {
#if defined(_WIN32)
            ::closesocket (fd);
#else
            ::close (fd);
#endif
        }
        fd = TETHER_BAD_SOCKET;
        bound = 0;
#if defined(_WIN32)
        if (wsa) { WSACleanup(); wsa = false; }
#endif
    }

    bool isOpen() const noexcept { return fd != TETHER_BAD_SOCKET; }
    uint16_t port() const noexcept { return bound; }

    bool send (const Address& to, const void* data, size_t n)
    {
        if (! isOpen() || ! to.valid) return false;
        const auto r = ::sendto (fd, (const char*) data, n, 0, (const sockaddr*) &to.sa, (socklen_t) sizeof (to.sa));
        return r >= 0 && (size_t) r == n;
    }

    // waits up to timeoutMs for one datagram; returns its size, 0 on time-out, -1 on error
    int receive (void* buf, size_t cap, sockaddr_in& from, int timeoutMs)
    {
        if (! isOpen()) return -1;
#if defined(_WIN32)
        WSAPOLLFD p { fd, POLLRDNORM, 0 };
        const int r = WSAPoll (&p, 1, timeoutMs);
#else
        pollfd p { fd, POLLIN, 0 };
        const int r = ::poll (&p, 1, timeoutMs);
#endif
        if (r <= 0) return r == 0 ? 0 : -1;
        socklen_t len = sizeof (from);
        const auto n = ::recvfrom (fd, (char*) buf, cap, 0, (sockaddr*) &from, &len);
        return n < 0 ? -1 : (int) n;
    }

    // "norns.local", "192.168.1.20"; IPv4 only (matron listens on IPv4)
    static Address resolve (const std::string& host, uint16_t port)
    {
        Address out;
        if (host.empty()) return out;
        out.sa.sin_family = AF_INET;
        out.sa.sin_port = htons (port);
        if (inet_pton (AF_INET, host.c_str(), &out.sa.sin_addr) == 1) { out.valid = true; return out; }
        addrinfo hints {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        if (getaddrinfo (host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) return out;
        out.sa.sin_addr = ((sockaddr_in*) res->ai_addr)->sin_addr;
        freeaddrinfo (res);
        out.valid = true;
        return out;
    }

private:
    tether_socket_t fd = TETHER_BAD_SOCKET;
    uint16_t bound = 0;
#if defined(_WIN32)
    bool wsa = false;
#endif
};

} // namespace tether
