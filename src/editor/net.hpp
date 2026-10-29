#pragma once
#include <string>
#include <vector>

namespace net {
using Sock = long long;
constexpr Sock kBad = -1;

bool init();
Sock listenTcp(int port, std::string &err);
Sock accept(Sock s, std::string *ip);
Sock connectTcp(const std::string &host, int port, std::string &err);
int connectDone(Sock s);
long recv(Sock s, char *buf, long n);
long send(Sock s, const char *buf, long n);
void close(Sock s);
Sock udpOpen(int bindPort);
bool udpSend(Sock s, const std::string &ip, int port, const std::string &msg);
long udpRecv(Sock s, char *buf, long n, std::string &fromIp);
struct Iface { std::string name, ip, broadcast; };
std::vector<Iface> interfaces();
}
