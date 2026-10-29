#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "project.hpp"

namespace collab {

enum class Role { None, Host, Client };

struct Peer {
    int id = 0;
    std::string name, doc;
    uint32_t color = 0;
    float x = 0, y = 0;
    bool cursor = false;
    std::vector<std::string> sel;
    json extra;
};
struct Found { std::string addr, host, project; int port = 0; double seen = 0; };
struct NetAddr { std::string ip, iface, kind; };

struct Events {
    bool docReplaced = false;
    bool changed = false;
    std::set<std::string> sprites;
    bool textures = false;
    std::string notice;
    bool noticeErr = false;
    bool ended = false;
    std::string chat;
};

constexpr int kPort = 47610;

Role role();
bool host(Project &proj, const std::string &name, uint32_t color, std::string &err);
bool join(const std::string &addr, const std::string &code, const std::string &name, uint32_t color, std::string &err);
void stop();
void update(Project &proj, Events &ev);
void setPresence(const std::string &doc, float x, float y, bool cursor, const std::vector<std::string> &sel, const json &extra = json());
void defaultPresence(const std::string &doc);

const std::vector<Peer> &peers();
const std::string &code();
std::string status();
bool connected();

void discover(bool on);
std::vector<Found> found();
std::vector<NetAddr> localAddrs();

bool hotspotStart(std::string &ssid, std::string &pass, std::string &out);
bool hotspotStop(std::string &out);

std::string firewallHint();
uint32_t colorFor(int i);

struct ChatMsg { int id = 0; std::string name; uint32_t color = 0; std::string text; double t = 0; };
void sendChat(const std::string &text, Events &ev);
const std::vector<ChatMsg> &chat();
int chatSinLeer();
void chatLeido();

void setAllowCopies(bool on);
bool allowCopies();
std::string defaultName();
}
