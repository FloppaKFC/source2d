#include "net.hpp"
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace net {
#ifdef _WIN32
using RawSock = SOCKET;
static bool wouldBlock() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY; }
static void setNonBlocking(RawSock s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
static RawSock raw(Sock s) { return (RawSock)s; }
static Sock wrap(RawSock s) { return s == INVALID_SOCKET ? kBad : (Sock)s; }
#else
using RawSock = int;
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
static void setNonBlocking(RawSock s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
static RawSock raw(Sock s) { return (RawSock)s; }
static Sock wrap(RawSock s) { return s < 0 ? kBad : (Sock)s; }
#endif

bool init() {
#ifdef _WIN32
    static bool done = false;
    if (!done) { WSADATA d; if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return false; done = true; }
#endif
    return true;
}

static void tuneTcp(RawSock s) {
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);   // el cursor no espera a llenar paquetes
    setNonBlocking(s);
}

Sock listenTcp(int port, std::string &err) {
    if (!init()) { err = "no se pudo iniciar la red"; return kBad; }
    RawSock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (wrap(s) == kBad) { err = "no se pudo crear el socket"; return kBad; }
#ifndef _WIN32
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr *)&a, sizeof a) != 0 || listen(s, 16) != 0) {
        err = "el puerto " + std::to_string(port) + " esta ocupado";
        close(wrap(s));
        return kBad;
    }
    setNonBlocking(s);
    return wrap(s);
}

Sock accept(Sock s, std::string *ip) {
    sockaddr_in a{};
    socklen_t l = sizeof a;
    RawSock c = ::accept(raw(s), (sockaddr *)&a, &l);
    if (wrap(c) == kBad) return kBad;
    tuneTcp(c);
    if (ip) { char b[64] = {0}; inet_ntop(AF_INET, &a.sin_addr, b, sizeof b); *ip = b; }
    return wrap(c);
}

Sock connectTcp(const std::string &host, int port, std::string &err) {
    if (!init()) { err = "no se pudo iniciar la red"; return kBad; }
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) { err = "no se encuentra " + host; return kBad; }
    RawSock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (wrap(s) == kBad) { freeaddrinfo(res); err = "no se pudo crear el socket"; return kBad; }
    tuneTcp(s);
    int r = ::connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0 && !wouldBlock()) { close(wrap(s)); err = "no se pudo conectar con " + host; return kBad; }
    return wrap(s);
}

int connectDone(Sock s) {
    fd_set w, e;
    FD_ZERO(&w); FD_ZERO(&e);
    FD_SET(raw(s), &w); FD_SET(raw(s), &e);
    timeval tv{0, 0};
    int r = select((int)raw(s) + 1, nullptr, &w, &e, &tv);
    if (r < 0) return -1;
    if (r == 0) return 0;
    int so = 0;
    socklen_t l = sizeof so;
    getsockopt(raw(s), SOL_SOCKET, SO_ERROR, (char *)&so, &l);
    return so == 0 && FD_ISSET(raw(s), &w) ? 1 : -1;
}

long recv(Sock s, char *buf, long n) {
    long r = (long)::recv(raw(s), buf, (int)n, 0);
    if (r > 0) return r;
    if (r == 0) return -1;                       // el otro lado cerro
    return wouldBlock() ? 0 : -1;
}

long send(Sock s, const char *buf, long n) {
#ifdef _WIN32
    long r = (long)::send(raw(s), buf, (int)n, 0);
#else
    long r = (long)::send(raw(s), buf, (size_t)n, MSG_NOSIGNAL);
#endif
    if (r >= 0) return r;
    return wouldBlock() ? 0 : -1;
}

void close(Sock s) {
    if (s == kBad) return;
#ifdef _WIN32
    closesocket(raw(s));
#else
    ::close(raw(s));
#endif
}

Sock udpOpen(int bindPort) {
    if (!init()) return kBad;
    RawSock s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (wrap(s) == kBad) return kBad;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);   // varios editores en la misma PC
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char *)&one, sizeof one);
#endif
    if (bindPort) {
        sockaddr_in a{};
        a.sin_family = AF_INET; a.sin_port = htons((unsigned short)bindPort); a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s, (sockaddr *)&a, sizeof a) != 0) { close(wrap(s)); return kBad; }
    }
    setNonBlocking(s);
    return wrap(s);
}

bool udpSend(Sock s, const std::string &ip, int port, const std::string &msg) {
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) return false;
    return ::sendto(raw(s), msg.data(), (int)msg.size(), 0, (sockaddr *)&a, sizeof a) >= 0;
}

long udpRecv(Sock s, char *buf, long n, std::string &fromIp) {
    sockaddr_in a{};
    socklen_t l = sizeof a;
    long r = (long)::recvfrom(raw(s), buf, (int)n, 0, (sockaddr *)&a, &l);
    if (r <= 0) return 0;
    char b[64] = {0};
    inet_ntop(AF_INET, &a.sin_addr, b, sizeof b);
    fromIp = b;
    return r;
}

static std::string ipStr(uint32_t hostOrder) {
    in_addr a; a.s_addr = htonl(hostOrder);
    char b[64] = {0};
    inet_ntop(AF_INET, &a, b, sizeof b);
    return b;
}

std::vector<Iface> interfaces() {
    std::vector<Iface> out;
#ifdef _WIN32
    if (!init()) return out;
    ULONG size = 32768;
    std::vector<unsigned char> buf(size);
    auto *aa = (IP_ADAPTER_ADDRESSES *)buf.data();
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, aa, &size) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size); aa = (IP_ADAPTER_ADDRESSES *)buf.data();
        if (GetAdaptersAddresses(AF_INET, flags, nullptr, aa, &size) != NO_ERROR) return out;
    }
    for (auto *a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        char name[256] = {0};
        WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, sizeof name, nullptr, nullptr);
        for (auto *u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            uint32_t ip = ntohl(((sockaddr_in *)u->Address.lpSockaddr)->sin_addr.s_addr);
            uint32_t mask = u->OnLinkPrefixLength >= 32 ? 0xFFFFFFFFu : u->OnLinkPrefixLength == 0 ? 0 : ~((1u << (32 - u->OnLinkPrefixLength)) - 1);
            out.push_back({name, ipStr(ip), ipStr(ip | ~mask)});
        }
    }
#else
    ifaddrs *ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return out;
    for (ifaddrs *i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
        uint32_t ip = ntohl(((sockaddr_in *)i->ifa_addr)->sin_addr.s_addr);
        std::string bc;
        if ((i->ifa_flags & IFF_BROADCAST) && i->ifa_broadaddr) bc = ipStr(ntohl(((sockaddr_in *)i->ifa_broadaddr)->sin_addr.s_addr));
        out.push_back({i->ifa_name, ipStr(ip), bc});
    }
    freeifaddrs(ifs);
#endif
    return out;
}
}
