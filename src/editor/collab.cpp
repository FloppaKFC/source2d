#include "collab.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include "assets.hpp"
#include "net.hpp"
#include "raylib.h"
#include "sys.hpp"

/* Protocolo: tramas [largo u32 LE][tipo u8][datos]; el largo cuenta el tipo. Los datos son JSON, salvo DOC (el
   proyecto en JSON comprimido con deflate). */
namespace collab {
namespace {
enum : uint8_t { HELLO = 1, WELCOME, DENY, DOC, PATCH, PRES, PEERS, HASH, RESYNC };
constexpr int kVersion = 1;
constexpr uint32_t kMaxFrame = 512u << 20;

struct Conn {
    net::Sock s = net::kBad;
    std::string in, out, ip;
    size_t outOff = 0;
    int id = 0;
    bool ready = false, connecting = false, dead = false, closeAfterFlush = false;
};

Role g_role = Role::None;
net::Sock g_listen = net::kBad, g_beacon = net::kBad, g_disc = net::kBad;
int g_port = kPort;
std::vector<Conn> g_conns;              // anfitrion: las personas conectadas; cliente: la conexion con el anfitrion
std::vector<Peer> g_peers;
Peer g_self;
std::string g_code, g_status, g_hostName, g_joinCode;
json g_shadow;                          // ultima version del proyecto ya enviada (con lo recibido aplicado)
bool g_haveShadow = false, g_connected = false, g_presDirty = true, g_discover = false;
int g_nextId = 2, g_pending = 0;        // cliente: cambios propios que el anfitrion aun no devolvio
uint64_t g_seq = 0, g_hashSeq = ~0ull;
double g_lastDiff = 0, g_lastPres = 0, g_lastBeacon = 0, g_lastHash = 0, g_lastResync = -100;
std::map<std::string, Found> g_found;

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void queue(Conn &c, uint8_t type, const std::string &data) {
    uint32_t len = (uint32_t)data.size() + 1;
    char h[5] = { (char)(len & 255), (char)(len >> 8 & 255), (char)(len >> 16 & 255), (char)(len >> 24 & 255), (char)type };
    c.out.append(h, 5);
    c.out += data;
}
void queueJson(Conn &c, uint8_t type, const json &j) { queue(c, type, j.dump()); }
void flush(Conn &c) {
    while (!c.dead && c.outOff < c.out.size()) {
        long n = net::send(c.s, c.out.data() + c.outOff, (long)std::min<size_t>(c.out.size() - c.outOff, 1 << 20));
        if (n < 0) { c.dead = true; return; }
        if (n == 0) break;
        c.outOff += (size_t)n;
    }
    if (c.outOff == c.out.size()) { c.out.clear(); c.outOff = 0; if (c.closeAfterFlush) c.dead = true; }
    else if (c.outOff > (4u << 20)) { c.out.erase(0, c.outOff); c.outOff = 0; }
}
void pump(Conn &c) {
    static char buf[1 << 16];
    for (int k = 0; k < 4096 && !c.dead; k++) {             // como mucho 256 MB por fotograma
        long n = net::recv(c.s, buf, sizeof buf);
        if (n > 0) c.in.append(buf, (size_t)n);
        else { if (n < 0) c.dead = true; break; }
    }
}
bool nextFrame(Conn &c, uint8_t &type, std::string &data) {
    if (c.in.size() < 5) return false;
    const unsigned char *p = (const unsigned char *)c.in.data();
    uint32_t len = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
    if (len == 0 || len > kMaxFrame) { c.dead = true; return false; }
    if (c.in.size() < 4 + (size_t)len) return false;
    type = p[4];
    data.assign(c.in, 5, len - 1);
    c.in.erase(0, 4 + (size_t)len);
    return true;
}

std::string packDoc(const json &doc) {
    std::string raw = doc.dump();
    int n = 0;
    unsigned char *z = CompressData((const unsigned char *)raw.data(), (int)raw.size(), &n);
    std::string out = z ? std::string((const char *)z, (size_t)n) : std::string();
    MemFree(z);
    return out;
}
bool unpackDoc(const std::string &z, json &doc) {
    int n = 0;
    unsigned char *d = DecompressData((const unsigned char *)z.data(), (int)z.size(), &n);
    if (!d) return false;
    doc = json::parse(d, d + n, nullptr, false);
    MemFree(d);
    return doc.is_object();
}

/* huella del proyecto: el orden de las claves de los objetos no cuenta (puede variar entre equipos) */
uint64_t mix(uint64_t h) { h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; return h ^ (h >> 33); }
uint64_t hashOf(const json &j) {
    switch (j.type()) {
    case json::value_t::object: { uint64_t h = 0x1234; for (auto it = j.begin(); it != j.end(); ++it) h += mix(std::hash<std::string>{}(it.key()) * 31 + hashOf(it.value())); return mix(h); }
    case json::value_t::array: { uint64_t h = 0x5678; for (auto &v : j) h = mix(h * 1099511628211ull ^ hashOf(v)); return h; }
    case json::value_t::string: return mix(std::hash<std::string>{}(j.get_ref<const std::string &>()));
    case json::value_t::boolean: return j.get<bool>() ? 3 : 2;
    case json::value_t::null: return 1;
    default: {
        double d = j.get<double>();
        if (d == (double)(long long)d) return mix((uint64_t)(long long)d + 7);
        uint64_t b; std::memcpy(&b, &d, sizeof b); return mix(b);
    }
    }
}

/* aplica las operaciones una a una (si una no encaja, se salta: la huella lo arreglara) en el proyecto y en la copia */
void applyOps(Project &proj, const json &ops, Events &ev, bool onlyIdempotent) {
    if (!ops.is_array()) return;
    for (auto &op : ops) {
        std::string path = jstr(op, "path"), kind = jstr(op, "op");
        if (onlyIdempotent) {
            /* cambios propios que vuelven del anfitrion: se reafirman los que dan igual repetir (valores y claves
               de objetos), asi el orden final es el del anfitrion; anadir o quitar en listas no se repite */
            if (kind == "add") {
                size_t sl = path.rfind('/');
                json::json_pointer parent(sl == std::string::npos || sl == 0 ? "" : path.substr(0, sl));
                if (!proj.doc.contains(parent) || !proj.doc[parent].is_object()) continue;
            } else if (kind != "replace") continue;
        }
        json one = json::array({op});
        try { proj.doc.patch_inplace(one); } catch (...) { continue; }
        try { g_shadow.patch_inplace(one); } catch (...) {}
        ev.changed = true;
        if (path.rfind("/sprites", 0) == 0) {
            ev.textures = true;
            size_t a = path.find('/', 1), b = a == std::string::npos ? a : path.find('/', a + 1);
            if (a != std::string::npos) {
                int idx = std::atoi(path.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1).c_str());
                if (proj.doc["sprites"].is_array() && idx >= 0 && idx < (int)proj.doc["sprites"].size()) ev.sprites.insert(jstr(proj.doc["sprites"][idx], "id"));
            } else ev.sprites.insert("*");
        }
        if (path.rfind("/tilesets", 0) == 0 || path.rfind("/fonts", 0) == 0) ev.textures = true;
    }
}

Peer *peerById(int id) {
    for (auto &p : g_peers) if (p.id == id) return &p;
    return nullptr;
}
json presJson(const Peer &p) {
    json j = {{"id", p.id}, {"name", p.name}, {"color", p.color}, {"doc", p.doc}, {"x", p.x}, {"y", p.y}, {"c", p.cursor}, {"sel", p.sel}};
    if (!p.extra.is_null()) j["e"] = p.extra;
    return j;
}
void readPres(Peer &p, const json &j) {
    p.doc = jstr(j, "doc"); p.x = (float)jnum(j, "x", 0); p.y = (float)jnum(j, "y", 0); p.cursor = jbool(j, "c", false);
    p.sel.clear();
    if (j.contains("sel") && j["sel"].is_array()) for (auto &s : j["sel"]) if (s.is_string()) p.sel.push_back(s.get<std::string>());
    p.extra = j.contains("e") ? j["e"] : json();
}
json peersJson() {
    json a = json::array();
    a.push_back({{"id", g_self.id}, {"name", g_self.name}, {"color", g_self.color}});
    for (auto &p : g_peers) a.push_back({{"id", p.id}, {"name", p.name}, {"color", p.color}});
    return a;
}
void broadcast(uint8_t type, const std::string &data, int except = -1) {
    for (auto &c : g_conns) if (c.ready && c.id != except) queue(c, type, data);
}

/* cambios propios: diferencia con la copia; despues de esto la copia es igual al proyecto. Se compara por secciones:
   las pesadas (archivos, musica y fuentes incrustados, que casi nunca cambian) una por segundo y por turnos, para no
   frenar el editor con proyectos grandes; el resto en cada pasada. */
std::string ptrKey(const std::string &k) {
    std::string o = "/";
    for (char ch : k) { if (ch == '~') o += "~0"; else if (ch == '/') o += "~1"; else o += ch; }
    return o;
}
void sendLocalChanges(Project &proj, bool force = false) {
    if (!g_haveShadow) return;
    static const char *const heavy[] = { "files", "music", "fonts" };
    static int turn = 0;
    static double lastHeavy = 0;
    const char *heavyNow = nullptr;
    if (force || now() - lastHeavy > 1.0) { lastHeavy = now(); heavyNow = heavy[turn++ % 3]; }
    auto isHeavy = [](const std::string &k) { for (auto *h : heavy) if (k == h) return true; return false; };
    json ops = json::array();
    if (!proj.doc.is_object() || !g_shadow.is_object()) ops = json::diff(g_shadow, proj.doc);
    else {
        for (auto it = g_shadow.begin(); it != g_shadow.end(); ++it)
            if (!proj.doc.contains(it.key())) ops.push_back({{"op", "remove"}, {"path", ptrKey(it.key())}});
        for (auto it = proj.doc.begin(); it != proj.doc.end(); ++it) {
            const std::string &k = it.key();
            if (!g_shadow.contains(k)) { ops.push_back({{"op", "add"}, {"path", ptrKey(k)}, {"value", it.value()}}); continue; }
            if (isHeavy(k) && !force && (!heavyNow || k != heavyNow)) continue;
            for (auto &op : json::diff(g_shadow[k], it.value(), ptrKey(k))) ops.push_back(op);
        }
    }
    if (ops.empty()) return;
    try { g_shadow.patch_inplace(ops); } catch (...) { g_shadow = proj.doc; }
    if (g_role == Role::Host) { broadcast(PATCH, json{{"from", g_self.id}, {"ops", ops}}.dump()); g_seq++; }
    else if (!g_conns.empty()) { queueJson(g_conns[0], PATCH, {{"ops", ops}}); g_pending++; }
}

void finish(Events &ev, const std::string &msg, bool err) {
    ev.ended = true; ev.notice = msg; ev.noticeErr = err;
    stop();
}

void updateHost(Project &proj, Events &ev) {
    std::string ip;
    for (net::Sock s; (s = net::accept(g_listen, &ip)) != net::kBad;) { Conn c; c.s = s; c.ip = ip; g_conns.push_back(std::move(c)); }
    for (auto &c : g_conns) {
        pump(c);
        uint8_t t; std::string d;
        while (!c.dead && nextFrame(c, t, d)) {
            if (t == DOC) continue;
            json j = json::parse(d, nullptr, false);
            if (t == HELLO) {
                if (jnum(j, "v", 0) != kVersion) { queueJson(c, DENY, {{"msg", "Ese editor es de otra versión de Source 2D"}}); c.closeAfterFlush = true; continue; }
                if (jstr(j, "code") != g_code) { queueJson(c, DENY, {{"msg", "El código de la sesión no es correcto"}}); c.closeAfterFlush = true; continue; }
                c.id = g_nextId++; c.ready = true;
                Peer p; p.id = c.id; p.name = jstr(j, "name", "Invitado"); p.color = (uint32_t)jnum(j, "color", colorFor(c.id));
                g_peers.push_back(p);
                queueJson(c, WELCOME, {{"id", c.id}, {"host", g_self.name}, {"peers", peersJson()}});
                queue(c, DOC, packDoc(proj.doc));
                broadcast(PEERS, peersJson().dump(), c.id);
                for (auto &o : g_peers) if (o.id != c.id) queueJson(c, PRES, presJson(o));
                queueJson(c, PRES, presJson(g_self));
                ev.notice = p.name + " se unió a la sesión";
                g_hashSeq = ~0ull;
            } else if (!c.ready) {
                c.dead = true;
            } else if (t == PATCH) {
                applyOps(proj, j["ops"], ev, false);
                broadcast(PATCH, json{{"from", c.id}, {"ops", j["ops"]}}.dump());
                g_seq++;
            } else if (t == PRES) {
                if (Peer *p = peerById(c.id)) { readPres(*p, j); j["id"] = c.id; j["name"] = p->name; j["color"] = p->color; broadcast(PRES, j.dump(), c.id); }
            } else if (t == RESYNC) {
                queue(c, DOC, packDoc(proj.doc));
            }
        }
    }
    bool left = false;
    for (size_t i = 0; i < g_conns.size();) {
        Conn &c = g_conns[i];
        if (!c.dead) flush(c);
        if (!c.dead) { i++; continue; }
        if (Peer *p = c.ready ? peerById(c.id) : nullptr) {
            ev.notice = p->name + " salió de la sesión";
            g_peers.erase(g_peers.begin() + (p - g_peers.data()));
            left = true;
        }
        net::close(c.s);
        g_conns.erase(g_conns.begin() + i);
    }
    if (left) broadcast(PEERS, peersJson().dump());
    double t = now();
    if (t - g_lastBeacon > 1.0) {
        g_lastBeacon = t;
        std::string msg = "S2DCOLAB1\n" + std::to_string(g_port) + "\n" + g_self.name + "\n" + proj.name();
        net::udpSend(g_beacon, "255.255.255.255", kPort + 1, msg);
        net::udpSend(g_beacon, "127.0.0.1", kPort + 1, msg);                 /* otro editor en esta misma PC */
        for (auto &i : net::interfaces()) if (!i.broadcast.empty()) net::udpSend(g_beacon, i.broadcast, kPort + 1, msg);
    }
    if (!g_conns.empty() && g_seq != g_hashSeq && t - g_lastHash > 3.0) {
        g_lastHash = t; g_hashSeq = g_seq;
        broadcast(HASH, json{{"h", std::to_string(hashOf(g_shadow))}}.dump());   /* lo ya enviado (sin lo que se edita ahora) */
    }
}

void updateClient(Project &proj, Events &ev) {
    if (g_conns.empty()) return;
    Conn &c = g_conns[0];
    if (c.connecting) {
        int r = net::connectDone(c.s);
        if (r < 0) { finish(ev, "No se pudo conectar con la sesión", true); return; }
        if (r == 0) return;
        c.connecting = false;
        queueJson(c, HELLO, {{"v", kVersion}, {"name", g_self.name}, {"color", g_self.color}, {"code", g_joinCode}});
        g_status = "Esperando al anfitrión...";
    }
    pump(c);
    uint8_t t; std::string d;
    while (nextFrame(c, t, d)) {                     /* tambien lo que llego justo antes de cerrarse (DENY) */
        if (t == DOC) {
            json doc;
            if (!unpackDoc(d, doc)) { finish(ev, "El proyecto de la sesión llegó dañado", true); return; }
            proj.doc = std::move(doc);
            proj.path.clear(); proj.dirty = false; proj.rev++;
            g_shadow = proj.doc; g_haveShadow = true; g_pending = 0;
            if (!g_connected) ev.notice = "Conectado con " + g_hostName;
            g_connected = true;
            g_status = "Conectado con " + g_hostName;
            ev.docReplaced = true;
            continue;
        }
        json j = json::parse(d, nullptr, false);
        if (t == WELCOME) {
            g_self.id = (int)jnum(j, "id", 0);
            g_hostName = jstr(j, "host", "el anfitrión");
            g_status = "Recibiendo el proyecto...";
        } else if (t == DENY) {
            finish(ev, jstr(j, "msg", "El anfitrión no te dejó entrar"), true);
            return;
        } else if (t == PEERS) {
            std::vector<Peer> np;
            for (auto &e : j) {
                int id = (int)jnum(e, "id", 0);
                if (id == g_self.id) continue;
                Peer p; if (Peer *o = peerById(id)) p = *o;
                p.id = id; p.name = jstr(e, "name"); p.color = (uint32_t)jnum(e, "color", 0);
                np.push_back(p);
            }
            for (auto &o : g_peers) if (std::none_of(np.begin(), np.end(), [&](const Peer &p) { return p.id == o.id; })) ev.notice = o.name + " salió de la sesión";
            for (auto &p : np) if (!peerById(p.id) && g_connected) ev.notice = p.name + " se unió a la sesión";
            g_peers = np;
        } else if (t == PATCH && g_connected) {
            bool mine = (int)jnum(j, "from", 0) == g_self.id;
            if (mine && g_pending > 0) g_pending--;
            applyOps(proj, j["ops"], ev, mine);
        } else if (t == PRES) {
            int id = (int)jnum(j, "id", 0);
            if (id == g_self.id) continue;
            Peer *p = peerById(id);
            if (!p) { Peer np; np.id = id; g_peers.push_back(np); p = &g_peers.back(); }
            p->name = jstr(j, "name", p->name); p->color = (uint32_t)jnum(j, "color", p->color);
            readPres(*p, j);
        } else if (t == HASH && g_connected && g_pending == 0) {
            if (jstr(j, "h") != std::to_string(hashOf(g_shadow)) && now() - g_lastResync > 10) {
                g_lastResync = now();
                queue(c, RESYNC, "{}");
            }
        }
    }
    flush(c);
    if (c.dead) finish(ev, g_connected ? "Se perdió la conexión con " + g_hostName + " (el proyecto sigue abierto, sin guardar)" : "No se pudo entrar en la sesión", true);
}

void pollDiscovery() {
    if (!g_discover || g_disc == net::kBad) return;
    char buf[1024];
    std::string ip;
    for (long n; (n = net::udpRecv(g_disc, buf, sizeof buf - 1, ip)) > 0;) {
        buf[n] = 0;
        std::vector<std::string> f;
        for (char *p = buf, *q; p; p = q ? q + 1 : nullptr) { q = std::strchr(p, '\n'); f.push_back(q ? std::string(p, q) : std::string(p)); }
        if (f.size() < 4 || f[0] != "S2DCOLAB1") continue;
        Found &e = g_found[ip + ":" + f[1]];
        e.addr = ip; e.port = std::atoi(f[1].c_str()); e.host = f[2]; e.project = f[3]; e.seen = now();
    }
}
}  // namespace

Role role() { return g_role; }
const std::vector<Peer> &peers() { return g_peers; }
const std::string &code() { return g_code; }
bool connected() { return g_role == Role::Client && g_connected; }
std::string status() {
    if (g_role == Role::Host) return "Compartiendo (" + std::to_string(g_peers.size()) + (g_peers.size() == 1 ? " persona" : " personas") + " conectadas)";
    return g_status;
}

bool host(Project &proj, const std::string &name, uint32_t color, std::string &err) {
    stop();
    for (int p = kPort; p < kPort + 10 && g_listen == net::kBad; p += 2) { g_listen = net::listenTcp(p, err); g_port = p; }
    if (g_listen == net::kBad) return false;
    err.clear();
    g_beacon = net::udpOpen(0);
    std::random_device rd;
    const char *fixed = std::getenv("S2D_COLAB_CODE");             // pruebas automaticas
    g_code = fixed && *fixed ? fixed : std::to_string(100000 + rd() % 900000);
    g_self = Peer{}; g_self.id = 1; g_self.name = name; g_self.color = color;
    g_peers.clear(); g_nextId = 2; g_seq = 0; g_hashSeq = ~0ull;
    g_shadow = proj.doc; g_haveShadow = true;
    g_role = Role::Host; g_presDirty = true;
    return true;
}

bool join(const std::string &addr, const std::string &code, const std::string &name, uint32_t color, std::string &err) {
    stop();
    std::string h = addr;
    int port = kPort;
    size_t c = h.rfind(':');
    if (c != std::string::npos && h.find(':') == c) { port = std::atoi(h.c_str() + c + 1); h = h.substr(0, c); }
    while (!h.empty() && h.back() == ' ') h.pop_back();
    while (!h.empty() && h.front() == ' ') h.erase(0, 1);
    if (h.empty()) { err = "Escribe la dirección del anfitrión"; return false; }
    net::Sock s = net::connectTcp(h, port, err);
    if (s == net::kBad) return false;
    Conn cn; cn.s = s; cn.connecting = true; cn.ip = h;
    g_conns.push_back(std::move(cn));
    g_self = Peer{}; g_self.name = name; g_self.color = color;
    g_joinCode = code; g_hostName = h;
    g_peers.clear(); g_connected = false; g_haveShadow = false; g_pending = 0;
    g_role = Role::Client; g_presDirty = true;
    g_status = "Conectando con " + h + "...";
    return true;
}

void stop() {
    for (auto &c : g_conns) { flush(c); net::close(c.s); }
    g_conns.clear();
    net::close(g_listen); g_listen = net::kBad;
    net::close(g_beacon); g_beacon = net::kBad;
    g_peers.clear();
    g_role = Role::None; g_connected = false; g_haveShadow = false; g_shadow = json();
    g_code.clear(); g_status.clear();
}

void update(Project &proj, Events &ev) {
    pollDiscovery();
    if (g_role == Role::None) return;
    double t = now();
    /* primero lo propio (asi la copia queda igual al proyecto) y despues lo que llega */
    if (t - g_lastDiff > 0.15 && (g_role == Role::Host || g_connected)) { g_lastDiff = t; sendLocalChanges(proj); }
    if (g_role == Role::Host) updateHost(proj, ev); else updateClient(proj, ev);
    if (g_role == Role::None) return;
    if ((g_presDirty && t - g_lastPres > 0.08) || t - g_lastPres > 2.0) {
        g_lastPres = t; g_presDirty = false;
        json pj = presJson(g_self);
        if (g_role == Role::Host) broadcast(PRES, pj.dump());
        else if (g_connected) queueJson(g_conns[0], PRES, pj);
    }
    for (auto &c : g_conns) flush(c);
}

static bool g_claimed = false;
void defaultPresence(const std::string &doc) {
    if (!g_claimed) setPresence(doc, 0, 0, false, {}, json());
    g_claimed = false;
}
void setPresence(const std::string &doc, float x, float y, bool cursor, const std::vector<std::string> &sel, const json &extra) {
    g_claimed = true;
    if (doc == g_self.doc && cursor == g_self.cursor && sel == g_self.sel && extra == g_self.extra &&
        (!cursor || (std::fabs(x - g_self.x) < 0.5f && std::fabs(y - g_self.y) < 0.5f))) return;
    g_self.doc = doc; g_self.x = x; g_self.y = y; g_self.cursor = cursor; g_self.sel = sel; g_self.extra = extra;
    g_presDirty = true;
}

void discover(bool on) {
    if (on == g_discover) return;
    g_discover = on;
    if (on) { g_disc = net::udpOpen(kPort + 1); g_found.clear(); }
    else { net::close(g_disc); g_disc = net::kBad; }
}
std::vector<Found> found() {
    std::vector<Found> out;
    double t = now();
    for (auto &kv : g_found) if (t - kv.second.seen < 4.0) out.push_back(kv.second);
    return out;
}

std::vector<NetAddr> localAddrs() {
    std::vector<NetAddr> out;
    for (auto &i : net::interfaces()) {
        std::string n = i.name, low;
        for (char ch : n) low += (char)std::tolower((unsigned char)ch);
        unsigned a = 0, b = 0;
        std::sscanf(i.ip.c_str(), "%u.%u", &a, &b);
        std::string kind = "Otra";
        if ((a == 100 && b >= 64 && b < 128) || low.find("tailscale") != std::string::npos) kind = "Tailscale";
        else if (low.rfind("bnep", 0) == 0 || low.find("bluetooth") != std::string::npos || low.rfind("pan", 0) == 0) kind = "Bluetooth";
        else if (i.ip.rfind("10.42.0.", 0) == 0 || i.ip.rfind("192.168.137.", 0) == 0 || low.rfind("ap", 0) == 0) kind = "Zona WiFi";
        else if (low.rfind("wl", 0) == 0 || low.find("wi-fi") != std::string::npos || low.find("wlan") != std::string::npos || low.find("inal") != std::string::npos) kind = "WiFi";
        else if (low.rfind("en", 0) == 0 || low.rfind("eth", 0) == 0 || low.find("ethernet") != std::string::npos) kind = "Cable";
        else if (low.rfind("docker", 0) == 0 || low.rfind("veth", 0) == 0 || low.rfind("br-", 0) == 0 || low.rfind("virbr", 0) == 0) continue;
        out.push_back({i.ip, i.name, kind});
    }
    return out;
}

bool hotspotStart(std::string &ssid, std::string &pass, std::string &out) {
#ifdef _WIN32
    (void)ssid; (void)pass;
    sys::Proc p;
    sys::spawn({"cmd", "/c", "start", "", "ms-settings:network-mobilehotspot"}, &p);
    out = "Se abrió Configuración > Zona con cobertura inalámbrica móvil: actívala y conecta la otra PC a esa red.";
    return true;
#else
    std::random_device rd;
    const char *al = "abcdefghjkmnpqrstuvwxyz23456789";
    ssid = "Source2D-" + std::to_string(1000 + rd() % 9000);
    pass.clear();
    for (int i = 0; i < 10; i++) pass += al[rd() % 31];
    int r = sys::run({"nmcli", "device", "wifi", "hotspot", "con-name", "Source2D", "ssid", ssid, "password", pass});
    if (r != 0) { out = "No se pudo crear la zona WiFi (hace falta NetworkManager y una tarjeta WiFi que lo permita)."; return false; }
    out = "Zona WiFi creada. En la otra PC conectate a la red \"" + ssid + "\" con la contraseña " + pass + ".";
    return true;
#endif
}
bool hotspotStop(std::string &out) {
#ifdef _WIN32
    out = "Apaga la zona en Configuración > Zona con cobertura inalámbrica móvil.";
    return true;
#else
    int r = sys::run({"nmcli", "connection", "down", "Source2D"});
    out = r == 0 ? "Zona WiFi apagada." : "No había ninguna zona WiFi de Source 2D encendida.";
    return r == 0;
#endif
}

std::string firewallHint() {
#ifdef _WIN32
    return "";                          /* Windows pregunta solo la primera vez que el editor comparte: hay que Permitir */
#else
    static int active = -1;
    if (active < 0) active = sys::run({"systemctl", "is-active", "--quiet", "ufw"}) == 0;
    if (!active) return "";
    return "El cortafuegos de esta PC (ufw) no deja entrar a los demás. Abre los puertos una vez con:\n"
           "  sudo ufw allow " + std::to_string(kPort) + ":" + std::to_string(kPort + 9) + "/tcp && sudo ufw allow " + std::to_string(kPort + 1) + "/udp";
#endif
}

uint32_t colorFor(int i) {
    static const uint32_t pal[] = { 0xFF5C8DFFu, 0xFF4FD08Bu, 0xFF3C9DFFu, 0xFFE07AF0u, 0xFF4ED9F5u, 0xFF7A6BFFu, 0xFFB0E040u, 0xFF8080FFu };
    return pal[(unsigned)i % 8];
}
std::string defaultName() {
    const char *u = std::getenv("USER");
    if (!u || !*u) u = std::getenv("USERNAME");
    std::string n = u && *u ? u : "Invitado";
    if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
    return n;
}
}
