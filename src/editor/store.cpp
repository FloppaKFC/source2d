#include "store.hpp"
#include "exporter.hpp"
#include "firma_publica.hpp"
#include "sound_studio.hpp"
#include "sys.hpp"
#include "ui.hpp"
#include "../../third_party/monocypher/monocypher.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace store {
namespace {
struct Def { const char *nombre, *base, *releases, *envVar; };
const Def kDefs[2] = {
    {"sprites", "https://raw.githubusercontent.com/FloppaKFC/source2d-sprites/main/", "https://github.com/FloppaKFC/source2d-sprites/releases/download/", "S2D_STORE_SPRITES_URL"},
    {"sonidos", "https://raw.githubusercontent.com/FloppaKFC/source2d-sonidos/main/", "https://github.com/FloppaKFC/source2d-sonidos/releases/download/", "S2D_STORE_SONIDOS_URL"}};
const char *kContador = "https://abacus.jasoncameron.dev/", *kEspacio = "source2d-floppa";

struct Bajada { sys::Proc p; std::string dest, hash, id; long long tam = 0; bool vivo = false; int code = 0; char que = 0; };
struct Tienda {
    bool on = false;
    std::string base, estado;
    std::vector<Paquete> paquetes;
    std::vector<Bajada> meta, portadas, bajadas, cuentas, hits;
    std::map<std::string, std::string> errores;
    std::map<std::string, std::pair<long long, long long>> cuenta;
    std::set<std::string> gustan;
    double cuentasAt = -1e9;
    time_t ultima = 0;
};
Tienda g_t[2];
bool g_buscar = false, g_pruebas = false;
std::string g_contador = kContador;
uint8_t g_pub[32];

std::string dir(int t) { return ui::configDir() + "/source-store/" + kDefs[t].nombre; }
std::string leer(const std::string &p) {
    std::ifstream in(fs::u8path(p), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
bool fromHex(std::string s, uint8_t *b, size_t n) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    if (s.size() != 2 * n) return false;
    for (size_t i = 0; i < n; i++) {
        char t[3] = {s[2 * i], s[2 * i + 1], 0}, *end;
        long v = strtol(t, &end, 16);
        if (*end) return false;
        b[i] = (uint8_t)v;
    }
    return true;
}
std::string huella(const std::string &p) {
    std::ifstream in(fs::u8path(p), std::ios::binary);
    if (!in) return "";
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 32);
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), buf.size());
        crypto_blake2b_update(&ctx, (const uint8_t *)buf.data(), (size_t)in.gcount());
    }
    uint8_t h[32];
    crypto_blake2b_final(&ctx, h);
    char x[65];
    for (int i = 0; i < 32; i++) snprintf(x + 2 * i, 3, "%02x", h[i]);
    return x;
}
bool rutaSegura(const std::string &s) {
    if (s.empty() || s.find("..") != std::string::npos || s[0] == '/') return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || strchr("_-./", c))) return false;
    return true;
}
bool idSeguro(const std::string &s) {
    if (s.empty() || s.size() > 60) return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) return false;
    return true;
}
bool lanzar(Bajada &b, const std::string &url, std::vector<std::string> extra) {
    std::vector<std::string> a = {"curl", "-fsL", "--proto", g_pruebas ? "=https,file" : "=https", "--proto-redir", "=https"};
    a.insert(a.end(), extra.begin(), extra.end());
    a.insert(a.end(), {"-o", b.dest, url});
    std::error_code ec;
    fs::create_directories(fs::u8path(b.dest).parent_path(), ec);
    fs::remove(fs::u8path(b.dest), ec);
    b.vivo = sys::spawn(a, &b.p, true);
    return b.vivo;
}
std::string sinCache(const std::string &url) { return url.rfind("https://", 0) == 0 ? url + "?t=" + std::to_string((long long)time(nullptr)) : url; }
double reloj() { return (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0; }

tienda::Archivo archivoDe(int t, const json &x) {
    tienda::Archivo a;
    if (!x.is_object()) return a;
    a.ruta = x.value("archivo", ""); a.blake2b = x.value("blake2b", "");
    a.tam = x.contains("tam") && x["tam"].is_number_integer() ? x["tam"].get<long long>() : 0;
    std::string url = x.value("url", "");
    uint8_t tmp[32];
    if (!rutaSegura(a.ruta) || !fromHex(a.blake2b, tmp, 32)) return tienda::Archivo{};
    if (url.empty()) a.url = g_t[t].base + a.ruta;
    else if ((url.rfind(kDefs[t].releases, 0) == 0 || (g_pruebas && url.rfind("file://", 0) == 0)) && url.find("..") == std::string::npos) a.url = url;
    else return tienda::Archivo{};
    return a;
}
bool firmado(const std::string &data, const std::string &firma, json &out) {
    uint8_t sig[64];
    if (data.empty() || !fromHex(firma, sig, 64) || crypto_eddsa_check(sig, g_pub, (const uint8_t *)data.data(), data.size()) != 0) return false;
    out = json::parse(data, nullptr, false);
    return out.is_object() && out.value("autor", "") == S2D_FIRMA_AUTOR;
}
void pedirPortada(int t, Paquete &o) {
    if (o.portada.url.empty()) return;
    std::string local = dir(t) + "/portadas/" + o.portada.blake2b + fs::u8path(o.portada.ruta).extension().u8string();
    std::error_code ec;
    if (fs::exists(fs::u8path(local), ec)) { o.portadaLocal = local; return; }
    if (!g_buscar) return;
    for (auto &b : g_t[t].portadas) if (b.hash == o.portada.blake2b) return;
    Bajada b; b.dest = local + ".parcial"; b.hash = o.portada.blake2b;
    if (lanzar(b, o.portada.url, {"--max-time", "60", "--max-filesize", "4000000"})) g_t[t].portadas.push_back(b);
}
void cargarCache(int t) {
    Tienda &T = g_t[t];
    T.paquetes.clear();
    json c;
    if (!firmado(leer(dir(t) + "/catalogo.json"), leer(dir(t) + "/catalogo.json.firma"), c) || c.value("tipo", "") != std::string("catalogo-") + kDefs[t].nombre) return;
    std::set<std::string> dest, ret;
    for (const char *k : {"destacados", "retirados"})
        if (c.contains(k) && c[k].is_array()) for (auto &x : c[k]) if (x.is_string()) (k[0] == 'd' ? dest : ret).insert(x.get<std::string>());
    if (c.contains("paquetes") && c["paquetes"].is_array()) for (auto &x : c["paquetes"]) {
        if (!x.is_object()) continue;
        Paquete o;
        o.id = x.value("id", ""); o.nombre = x.value("nombre", ""); o.autor = x.value("autor", ""); o.descripcion = x.value("descripcion", "");
        o.categoria = x.value("categoria", "Mixto"); o.licencia = x.value("licencia", ""); o.fecha = x.value("fecha", "");
        o.contador = x.value("contador", ""); o.version = x.value("version", 1);
        o.archivo = archivoDe(t, x.value("paquete", json())); o.portada = archivoDe(t, x.value("portada", json()));
        if (x.contains("contenido") && x["contenido"].is_object()) {
            auto &n = x["contenido"];
            o.nSprites = n.value("sprites", 0); o.nTiles = n.value("tilesets", 0); o.nAutotiles = n.value("autotiles", 0); o.nSonidos = n.value("sonidos", 0);
        }
        if (!idSeguro(o.id) || o.nombre.empty() || o.archivo.url.empty() || o.archivo.tam <= 0 || ret.count(o.id)) continue;
        if (!idSeguro(o.contador)) o.contador.clear();
        o.destacado = dest.count(o.id) > 0;
        pedirPortada(t, o);
        T.paquetes.push_back(o);
    }
    std::sort(T.paquetes.begin(), T.paquetes.end(), [](const Paquete &a, const Paquete &b) { return a.fecha != b.fecha ? a.fecha > b.fecha : a.nombre < b.nombre; });
    std::error_code ec;
    for (auto &id : ret) fs::remove_all(fs::u8path(dir(t) + "/instalados/" + id), ec);
}
long long valor(Bajada &b) {
    json j = json::parse(leer(b.dest), nullptr, false);
    std::error_code ec;
    fs::remove(fs::u8path(b.dest), ec);
    if (b.code == 22 || (g_pruebas && b.code == 37)) return 0;
    if (b.code != 0 || !j.is_object() || !j.contains("value") || !j["value"].is_number_integer()) return -1;
    return j["value"].get<long long>();
}
void sumar(int t, const Paquete &p, char que) {
    if (g_contador.empty() || p.contador.empty()) return;
    Bajada b; b.dest = dir(t) + "/contadores/hit-" + p.id + "-" + que + ".json"; b.id = p.id; b.que = que;
    if (lanzar(b, g_contador + "hit/" + kEspacio + "/" + p.contador + "-" + que, {"--max-time", "15", "--max-filesize", "4096"})) g_t[t].hits.push_back(b);
}
void guardarGustan(int t) {
    if (g_pruebas && !getenv(kDefs[t].envVar)) return;
    sys::writeDurable(dir(t) + "/local.json", json{{"gustan", g_t[t].gustan}}.dump());
}
std::string instDir(int t, const std::string &id) { return dir(t) + "/instalados/" + id; }
}

void iniciar(bool buscar, bool pruebas) {
    g_pruebas = pruebas;
    std::string pub = S2D_FIRMA_PUBLICA;
    if (pruebas) { const char *k = getenv("S2D_FIRMA_PUBLICA"); pub = k ? k : ""; }
    if (const char *c = getenv("S2D_CONTADOR_URL")) g_contador = c;
    else if (pruebas) g_contador.clear();
    if (!fromHex(pub, g_pub, 32)) return;
    g_buscar = buscar;
    for (int t = 0; t < 2; t++) {
        const char *url = getenv(kDefs[t].envVar);
        if (pruebas && !url) continue;
        Tienda &T = g_t[t];
        T.base = url && *url ? url : kDefs[t].base;
        T.on = true;
        std::error_code ec;
        if (pruebas) fs::remove_all(fs::u8path(dir(t)), ec);
        json l = json::parse(leer(dir(t) + "/local.json"), nullptr, false);
        if (l.is_object() && l.contains("gustan") && l["gustan"].is_array())
            for (auto &x : l["gustan"]) if (x.is_string()) T.gustan.insert(x.get<std::string>());
        cargarCache(t);
    }
}

void buscarAhora(Tipo t) {
    Tienda &T = g_t[t];
    if (!T.on || !g_buscar || !T.meta.empty()) return;
    T.ultima = time(nullptr);
    for (const char *nm : {"catalogo.json", "catalogo.json.firma"}) {
        Bajada b;
        b.dest = dir(t) + "/descarga/" + nm;
        if (!lanzar(b, sinCache(T.base + nm), {"--max-time", "30", "--max-filesize", "8000000"})) { T.estado = "No se encontró curl."; T.meta.clear(); return; }
        T.meta.push_back(b);
    }
    T.estado = "Buscando...";
}

void tick() {
    std::error_code ec;
    for (int t = 0; t < 2; t++) {
        Tienda &T = g_t[t];
        if (!T.on) continue;
        if (!T.meta.empty()) {
            bool all = true;
            for (auto &b : T.meta) if (b.vivo && !sys::finished(b.p, &b.code)) all = false; else b.vivo = false;
            if (all) {
                bool conectado = false;
                for (auto &b : T.meta) if (b.code == 0 || b.code == 22) conectado = true;
                std::string d = leer(dir(t) + "/descarga/catalogo.json"), f = leer(dir(t) + "/descarga/catalogo.json.firma");
                json c;
                bool ok = firmado(d, f, c) && c.value("tipo", "") == std::string("catalogo-") + kDefs[t].nombre;
                if (ok) { sys::writeDurable(dir(t) + "/catalogo.json", d); sys::writeDurable(dir(t) + "/catalogo.json.firma", f); }
                fs::remove_all(fs::u8path(dir(t) + "/descarga"), ec);
                cargarCache(t);
                T.estado = ok ? "" : !d.empty() ? "El catálogo no se pudo comprobar y no se muestra." :
                           conectado ? (T.paquetes.empty() ? "Todavía no hay paquetes publicados." : "") : "Sin conexión.";
                T.meta.clear();
            }
        }
        bool portada = false;
        for (auto it = T.portadas.begin(); it != T.portadas.end();) {
            if (it->vivo && !sys::finished(it->p)) { ++it; continue; }
            std::string fin = it->dest.substr(0, it->dest.size() - 8);
            if (huella(it->dest) == it->hash) { fs::rename(fs::u8path(it->dest), fs::u8path(fin), ec); portada = true; }
            else fs::remove(fs::u8path(it->dest), ec);
            it = T.portadas.erase(it);
        }
        if (portada) for (auto &p : T.paquetes) if (p.portadaLocal.empty()) pedirPortada(t, p);
        for (auto it = T.bajadas.begin(); it != T.bajadas.end();) {
            if (it->vivo && !sys::finished(it->p, &it->code)) { ++it; continue; }
            const Paquete *p = buscar((Tipo)t, it->id);
            long long sz = (long long)fs::file_size(fs::u8path(it->dest), ec);
            if (ec) sz = 0;
            json pk; std::string err;
            if (p && sz == it->tam && huella(it->dest) == it->hash && unpack(leer(it->dest), pk, err) && valido(pk, (Tipo)t, err)) {
                fs::rename(fs::u8path(it->dest), fs::u8path(instDir(t, it->id) + "/paquete.dat"), ec);
                sys::writeDurable(instDir(t, it->id) + "/info.json", json{{"id", p->id}, {"version", p->version}, {"blake2b", p->archivo.blake2b}}.dump());
                T.errores.erase(it->id);
                sumar(t, *p, 'd');
            } else {
                fs::remove(fs::u8path(it->dest), ec);
                T.errores[it->id] = it->code && it->code != 22 ? "No se pudo descargar. ¿Hay internet?" : err.empty() ? "El archivo no coincide con el catálogo y se borró." : err;
            }
            it = T.bajadas.erase(it);
        }
        for (auto *lista : {&T.cuentas, &T.hits})
            for (auto it = lista->begin(); it != lista->end();) {
                if (it->vivo && !sys::finished(it->p, &it->code)) { ++it; continue; }
                long long v = valor(*it);
                if (v >= 0) { auto &c = T.cuenta[it->id]; (it->que == 'd' ? c.first : c.second) = v; }
                it = lista->erase(it);
            }
        if (g_buscar && T.meta.empty() && T.ultima && time(nullptr) - T.ultima > 3600) buscarAhora((Tipo)t);
    }
}

std::string estado(Tipo t) { return g_t[t].estado; }
const std::vector<Paquete> &paquetes(Tipo t) { return g_t[t].paquetes; }
const Paquete *buscar(Tipo t, const std::string &id) {
    for (auto &p : g_t[t].paquetes) if (p.id == id) return &p;
    return nullptr;
}
bool instalado(Tipo t, const std::string &id) {
    std::error_code ec;
    return idSeguro(id) && fs::exists(fs::u8path(instDir(t, id) + "/paquete.dat"), ec);
}
bool descargando(Tipo t, const std::string &id) {
    for (auto &b : g_t[t].bajadas) if (b.id == id) return true;
    return false;
}
std::string error(Tipo t, const std::string &id) {
    auto it = g_t[t].errores.find(id);
    return it == g_t[t].errores.end() ? "" : it->second;
}
void descargar(Tipo t, const Paquete &p) {
    if (!g_t[t].on || descargando(t, p.id)) return;
    Bajada b; b.id = p.id; b.hash = p.archivo.blake2b; b.tam = p.archivo.tam;
    b.dest = instDir(t, p.id) + "/paquete.dat.parcial";
    g_t[t].errores.erase(p.id);
    if (lanzar(b, p.archivo.url, {"--max-time", "600", "--max-filesize", std::to_string(p.archivo.tam + 1)})) g_t[t].bajadas.push_back(b);
    else g_t[t].errores[p.id] = "No se encontró curl.";
}
void borrar(Tipo t, const std::string &id) {
    if (!idSeguro(id)) return;
    std::error_code ec;
    fs::remove_all(fs::u8path(instDir(t, id)), ec);
}
bool leer(Tipo t, const std::string &id, json &paquete, std::string &err) {
    if (!instalado(t, id)) { err = "No está descargado"; return false; }
    json info = json::parse(leer(instDir(t, id) + "/info.json"), nullptr, false);
    if (!info.is_object() || huella(instDir(t, id) + "/paquete.dat") != info.value("blake2b", "")) { err = "El paquete descargado está dañado: bórralo y descárgalo otra vez"; return false; }
    return unpack(leer(instDir(t, id) + "/paquete.dat"), paquete, err) && valido(paquete, t, err);
}

void mirarContadores(Tipo t) {
    Tienda &T = g_t[t];
    if (!T.on || !g_buscar || g_contador.empty() || !T.cuentas.empty() || reloj() - T.cuentasAt < 60) return;
    T.cuentasAt = reloj();
    int n = 0;
    for (auto &p : T.paquetes) {
        if (p.contador.empty() || n++ >= 40) continue;
        for (char q : {'d', 'g'}) {
            Bajada b; b.id = p.id; b.que = q; b.dest = dir(t) + "/contadores/" + p.id + "-" + q + ".json";
            if (lanzar(b, g_contador + "get/" + kEspacio + "/" + p.contador + "-" + q, {"--max-time", "15", "--max-filesize", "4096"})) T.cuentas.push_back(b);
        }
    }
}
long long descargas(Tipo t, const std::string &id) { auto it = g_t[t].cuenta.find(id); return it == g_t[t].cuenta.end() ? -1 : it->second.first; }
long long meGusta(Tipo t, const std::string &id) { auto it = g_t[t].cuenta.find(id); return it == g_t[t].cuenta.end() ? -1 : it->second.second; }
bool leGusta(Tipo t, const std::string &id) { return g_t[t].gustan.count(id) > 0; }
void darMeGusta(Tipo t, const Paquete &p) {
    if (g_t[t].gustan.count(p.id)) return;
    g_t[t].gustan.insert(p.id);
    guardarGustan(t);
    auto &c = g_t[t].cuenta[p.id];
    if (c.second >= 0) c.second++;
    sumar(t, p, 'g');
}

bool valido(const json &pk, Tipo t, std::string &err) {
    if (!pk.is_object() || pk.value("formato", "") != "source2d-paquete") { err = "No es un paquete de Source Store"; return false; }
    if (pk.value("tipo", "") != kDefs[t].nombre) { err = "Ese paquete es de otro tipo"; return false; }
    int n = 0;
    if (t == Sprites) {
        for (const char *k : {"sprites", "tilesets", "autotiles"}) {
            if (pk.contains(k) && !pk[k].is_array()) { err = "Paquete dañado"; return false; }
            if (pk.contains(k)) for (auto &x : pk[k]) { if (!x.is_object()) { err = "Paquete dañado"; return false; } n++; }
        }
        if (pk.contains("sonidos")) { err = "Un paquete de sprites no puede llevar sonidos"; return false; }
    } else {
        if (!pk.contains("sonidos") || !pk["sonidos"].is_array()) { err = "Paquete dañado"; return false; }
        for (auto &x : pk["sonidos"]) {
            if (!x.is_object() || x.value("format", "") != "source2d-sound") { err = "Solo se aceptan sonidos hechos en Source Sound"; return false; }
            n++;
        }
        for (const char *k : {"sprites", "tilesets", "autotiles"}) if (pk.contains(k)) { err = "Un paquete de sonidos solo lleva sonidos"; return false; }
    }
    if (!n) { err = "El paquete está vacío"; return false; }
    return true;
}

int anadirAlProyecto(Project &proj, const json &pk) {
    int n = 0;
    auto lista = [&](const char *k) -> json & {
        if (!proj.doc.contains(k) || !proj.doc[k].is_array()) proj.doc[k] = json::array();
        return proj.doc[k];
    };
    auto libre = [](json &l, std::string nm) {
        auto usado = [&](const std::string &x) { for (auto &e : l) if (jstr(e, "name") == x) return true; return false; };
        if (!usado(nm)) return nm;
        for (int i = 2;; i++) if (!usado(nm + " " + std::to_string(i))) return nm + " " + std::to_string(i);
    };
    if (pk.value("tipo", "") == "sonidos") {
        for (auto &s : pk["sonidos"]) { sound::saveToProject(proj, s, ""); n++; }
        proj.rev++;
        return n;
    }
    std::map<std::string, std::string> tsIds;
    if (pk.contains("tilesets")) for (auto x : pk["tilesets"]) {
        std::string nid = makeUid("ts");
        tsIds[jstr(x, "id")] = nid;
        x["id"] = nid; x["name"] = libre(lista("tilesets"), jstr(x, "name", "Tiles"));
        lista("tilesets").push_back(x); n++;
    }
    if (pk.contains("autotiles")) for (auto x : pk["autotiles"]) {
        x["id"] = makeUid("at"); x["name"] = libre(lista("autotiles"), jstr(x, "name", "Autotile"));
        auto it = tsIds.find(jstr(x, "tilesetId"));
        x["tilesetId"] = it == tsIds.end() ? "" : it->second;
        lista("autotiles").push_back(x); n++;
    }
    if (pk.contains("sprites")) for (auto x : pk["sprites"]) {
        x["id"] = makeUid("s"); x["name"] = libre(lista("sprites"), jstr(x, "name", "Sprite"));
        lista("sprites").push_back(x); n++;
    }
    proj.dirty = true; proj.rev++;
    return n;
}
std::string pack(const json &pk) { return exporter::sourcePack(pk); }
bool unpack(const std::string &bytes, json &pk, std::string &err) { return exporter::sourceUnpack(bytes, pk, err); }
}
