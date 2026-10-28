// Source 2D - sockets TCP y UDP sin bloqueo para Linux y Windows (lo usa collab.cpp).
// Va aparte porque en Windows winsock2.h (windows.h) choca con raylib.h.
#pragma once
#include <string>
#include <vector>

namespace net {
using Sock = long long;
constexpr Sock kBad = -1;

bool init();
// TCP
Sock listenTcp(int port, std::string &err);                      // escucha en todas las interfaces
Sock accept(Sock s, std::string *ip);                             // kBad si no hay nadie esperando
Sock connectTcp(const std::string &host, int port, std::string &err);   // empieza a conectar (sin esperar)
int connectDone(Sock s);                                          // 1 conectado, 0 todavia no, -1 fallo
long recv(Sock s, char *buf, long n);                             // >0 bytes, 0 nada por ahora, -1 cerrado o error
long send(Sock s, const char *buf, long n);                       // bytes enviados (0 si el bufer esta lleno), -1 error
void close(Sock s);
// UDP (avisos de sesion en la red local)
Sock udpOpen(int bindPort);                                       // bindPort 0 = solo para enviar
bool udpSend(Sock s, const std::string &ip, int port, const std::string &msg);
long udpRecv(Sock s, char *buf, long n, std::string &fromIp);
// interfaces IPv4 de este equipo
struct Iface { std::string name, ip, broadcast; };
std::vector<Iface> interfaces();
}
