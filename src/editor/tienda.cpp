// Source 2D - Source Play (ver tienda.hpp).
#include "tienda.hpp"
#include "cerrado.hpp"
#include "exporter.hpp"
#include "firma_publica.hpp"
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

namespace tienda {
namespace {
const char *kBase = "https://raw.githubusercontent.com/FloppaKFC/source2d-juegos/main/";
const char *kContador = "https://abacus.jasoncameron.dev/", *kEspacio = "source2d-floppa";
const char *kReleases = "https://github.com/FloppaKFC/source2d-juegos/releases/download/";
const time_t kCada = 3600;                   // el catalogo se vuelve a mirar cada hora si el editor sigue abierto
const double kContadoresCada = 60;

struct Bajada { sys::Proc p; std::string dest, hash, id; long long tam = 0; bool vivo = false; int code = 0; };
struct Cuenta { long long d = -1, g = -1; };

bool g_on = false, g_buscar = false, g_pruebas = false, g_hayCache = false;
std::string g_base = kBase, g_contador = kContador, g_estado;
uint8_t g_pub[32];
std::vector<Juego> g_juegos;
std::set<std::string> g_retirados;
std::map<std::string, int> g_insignias;     // login en minusculas -> 1 verificada, 2 admin
std::vector<Bajada> g_meta, g_portadas, g_juegosBaj, g_fuentes, g_cuentas, g_hits;
std::map<std::string, std::string> g_errores;
std::map<std::string, Cuenta> g_cuenta;
std::set<std::string> g_gustan;              // "me gusta" que dio este editor
std::set<std::string> g_favs;                // favoritos de esta persona
std::set<std::string> g_destacados;
double g_cuentasAt = -1e9;
time_t g_ultima = 0;

std::string dir() { return ui::configDir() + "/source-play"; }
std::string instDir(const std::string &id) { return dir() + "/instalados/" + id; }
double reloj() { return (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0; }
std::string sinCache(const std::string &url) {
    if (url.rfind("https://", 0) != 0) return url;
    return url + "?t=" + std::to_string((long long)time(nullptr));
}
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
// rutas e ids que vienen del catalogo: sin "..", sin barras al principio ni caracteres raros
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
Archivo archivoDe(const json &x) {
    Archivo a;
    if (!x.is_object()) return a;
    a.ruta = x.value("archivo", ""); a.blake2b = x.value("blake2b", "");
    a.tam = x.contains("tam") && x["tam"].is_number_integer() ? x["tam"].get<long long>() : 0;
    std::string url = x.value("url", "");
    uint8_t tmp[32];
    if (!rutaSegura(a.ruta) || !fromHex(a.blake2b, tmp, 32)) return Archivo{};
    // solo las Releases de source2d-juegos (en las pruebas, la carpeta file:// del catalogo)
    if (url.empty()) a.url = g_base + a.ruta;
    else if ((url.rfind(kReleases, 0) == 0 || (g_pruebas && url.rfind("file://", 0) == 0)) && url.find("..") == std::string::npos) a.url = url;
    else return Archivo{};
    return a;
}
json estadoLocal() {
    json j = json::parse(leer(dir() + "/local.json"), nullptr, false);
    return j.is_object() ? j : json::object();
}
void guardarLocal() {
    if (g_pruebas && !getenv("S2D_TIENDA_URL")) return;
    json j = estadoLocal();
    j["gustan"] = g_gustan;
    j["favoritos"] = g_favs;
    sys::writeDurable(dir() + "/local.json", j.dump());
}
json infoInstalado(const std::string &id) {
    json j = json::parse(leer(instDir(id) + "/info.json"), nullptr, false);
    return j.is_object() ? j : json();
}
void pedirPortada(Juego &o) {
    if (o.portada.ruta.empty()) return;
    std::string ext = fs::u8path(o.portada.ruta).extension().u8string();
    std::string local = dir() + "/portadas/" + o.portada.blake2b + ext;
    std::error_code ec;
    if (fs::exists(fs::u8path(local), ec)) { o.portadaLocal = local; return; }
    if (!g_buscar) return;
    for (auto &b : g_portadas) if (b.hash == o.portada.blake2b) return;
    Bajada b; b.dest = local + ".parcial"; b.hash = o.portada.blake2b;
    if (lanzar(b, o.portada.url, {"--max-time", "60", "--max-filesize", "4000000"})) g_portadas.push_back(b);
}

bool firmado(const std::string &data, const std::string &firma, const char *tipo, json &out) {
    uint8_t sig[64];
    if (data.empty() || !fromHex(firma, sig, 64) || crypto_eddsa_check(sig, g_pub, (const uint8_t *)data.data(), data.size()) != 0) return false;
    out = json::parse(data, nullptr, false);
    return out.is_object() && out.value("tipo", "") == tipo && out.value("autor", "") == S2D_FIRMA_AUTOR;
}
std::string minus(std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; }
void cargarCuentas() {
    g_insignias.clear();
    json c;
    if (!firmado(leer(dir() + "/cuentas.json"), leer(dir() + "/cuentas.json.firma"), "cuentas", c)) return;
    for (auto &[k, v] : std::map<std::string, int>{{"verificados", 1}, {"admins", 2}, {"creadores", 3}})
        if (c.contains(k) && c[k].is_array()) for (auto &x : c[k]) if (x.is_string()) g_insignias[minus(x)] = std::max(g_insignias[minus(x)], v);
}
void cargarCache() {
    cargarCuentas();
    g_juegos.clear(); g_retirados.clear(); g_hayCache = false;
    std::string data = leer(dir() + "/catalogo.json"), firma = leer(dir() + "/catalogo.json.firma");
    uint8_t sig[64];
    if (data.empty() || !fromHex(firma, sig, 64) || crypto_eddsa_check(sig, g_pub, (const uint8_t *)data.data(), data.size()) != 0) return;
    json c = json::parse(data, nullptr, false);
    if (!c.is_object() || c.value("tipo", "") != "catalogo" || c.value("autor", "") != S2D_FIRMA_AUTOR) return;
    g_hayCache = true;
    g_destacados.clear();
    if (c.contains("destacados") && c["destacados"].is_array())
        for (auto &r : c["destacados"]) if (r.is_string()) g_destacados.insert(r.get<std::string>());
    if (c.contains("retirados") && c["retirados"].is_array())
        for (auto &r : c["retirados"]) if (r.is_string() && idSeguro(r)) g_retirados.insert(r.get<std::string>());
    if (c.contains("juegos") && c["juegos"].is_array()) for (auto &x : c["juegos"]) {
        if (!x.is_object()) continue;
        Juego o;
        o.id = x.value("id", ""); o.nombre = x.value("nombre", ""); o.autor = x.value("autor", ""); o.descripcion = x.value("descripcion", "");
        o.categoria = x.value("categoria", "Otros"); o.fecha = x.value("fecha", ""); o.motor = x.value("motor", "");
        o.version = x.value("version", 1); o.abierto = x.value("abierto", true); o.contador = x.value("contador", "");
        o.juego = archivoDe(x.value("juego", json())); o.portada = archivoDe(x.value("portada", json()));
        if (!o.abierto) {
            o.llave = x.value("llave", ""); o.fuente = archivoDe(x.value("fuente", json()));
            uint8_t k[32];
            if (!fromHex(o.llave, k, 32)) continue;
        }
        if (!idSeguro(o.id) || o.nombre.empty() || o.juego.ruta.empty() || o.juego.tam <= 0 || g_retirados.count(o.id)) continue;
        if (!idSeguro(o.contador)) o.contador.clear();
        o.destacado = g_destacados.count(o.id) > 0;
        pedirPortada(o);
        g_juegos.push_back(o);
    }
    std::sort(g_juegos.begin(), g_juegos.end(), [](const Juego &a, const Juego &b) {
        if (a.fecha != b.fecha) return a.fecha > b.fecha;
        return a.nombre < b.nombre;
    });
    // los retirados se borran tambien de este equipo (por ejemplo, un juego que resulto peligroso)
    std::error_code ec;
    for (auto &id : g_retirados) if (fs::exists(fs::u8path(instDir(id)), ec)) fs::remove_all(fs::u8path(instDir(id)), ec);
}

void procesarBusqueda() {
    bool conectado = false;
    for (auto &b : g_meta) if (b.code == 0 || b.code == 22) conectado = true;
    std::string d = leer(dir() + "/descarga/catalogo.json"), f = leer(dir() + "/descarga/catalogo.json.firma");
    uint8_t sig[64];
    bool ok = !d.empty() && fromHex(f, sig, 64) && crypto_eddsa_check(sig, g_pub, (const uint8_t *)d.data(), d.size()) == 0;
    if (ok) {
        json c = json::parse(d, nullptr, false);
        ok = c.is_object() && c.value("tipo", "") == "catalogo" && c.value("autor", "") == S2D_FIRMA_AUTOR;
    }
    if (ok) {
        sys::writeDurable(dir() + "/catalogo.json", d);
        sys::writeDurable(dir() + "/catalogo.json.firma", f);
    } else if (!d.empty()) fprintf(stderr, "Source 2D: el catalogo de Source Play no viene firmado por " S2D_FIRMA_AUTOR ", se ignora\n");
    // las cuentas (verificadas y admins) van aparte: si aun no hay archivo, nadie tiene insignia
    std::string cd = leer(dir() + "/descarga/cuentas.json"), cf = leer(dir() + "/descarga/cuentas.json.firma");
    json cj;
    if (firmado(cd, cf, "cuentas", cj)) { sys::writeDurable(dir() + "/cuentas.json", cd); sys::writeDurable(dir() + "/cuentas.json.firma", cf); }
    std::error_code ec;
    fs::remove_all(fs::u8path(dir() + "/descarga"), ec);
    cargarCache();
    if (ok) g_estado.clear();
    else if (!d.empty()) g_estado = "El catálogo no se pudo comprobar y no se muestra.";
    else if (conectado) g_estado = g_hayCache ? "" : "Todavía no hay juegos publicados en Source Play.";
    else g_estado = g_hayCache ? "Sin conexión: se ven los juegos de la última vez." : "Sin conexión. Los juegos saldrán aquí cuando haya internet.";
}

const Juego *buscarJuego(const std::string &id) {
    for (auto &j : g_juegos) if (j.id == id) return &j;
    return nullptr;
}
std::string urlContador(const char *accion, const std::string &clave) { return g_contador + accion + "/" + kEspacio + "/" + clave; }
long long valor(Bajada &b) {
    json j = json::parse(leer(b.dest), nullptr, false);
    std::error_code ec;
    fs::remove(fs::u8path(b.dest), ec);
    if (b.code == 22 || (g_pruebas && b.code == 37)) return 0;
    if (b.code != 0 || !j.is_object() || !j.contains("value") || !j["value"].is_number_integer()) return -1;
    return j["value"].get<long long>();
}
void sumar(const std::string &clave, const std::string &id, char que) {
    if (g_contador.empty() || clave.empty()) return;
    Bajada b; b.dest = dir() + "/contadores/hit-" + id + "-" + que + ".json"; b.id = id; b.hash = std::string(1, que);
    if (lanzar(b, urlContador("hit", clave + "-" + que), {"--max-time", "15", "--max-filesize", "4096"})) g_hits.push_back(b);
}
}  // namespace

void iniciar(bool buscar, bool pruebas) {
    g_pruebas = pruebas;
    std::string pub = S2D_FIRMA_PUBLICA;
    const char *url = getenv("S2D_TIENDA_URL");
    if (pruebas) {
        const char *k = getenv("S2D_FIRMA_PUBLICA");
        pub = k ? k : "";
        if (!url) return;
    }
    if (url && *url) g_base = url;
    if (const char *c = getenv("S2D_CONTADOR_URL")) g_contador = c;
    else if (pruebas) g_contador.clear();
    if (!fromHex(pub, g_pub, 32)) return;
    g_on = true;
    g_buscar = buscar;
    if (pruebas) { std::error_code ec; fs::remove_all(fs::u8path(dir()), ec); }
    json l = estadoLocal();
    if (l.contains("gustan") && l["gustan"].is_array()) for (auto &x : l["gustan"]) if (x.is_string()) g_gustan.insert(x.get<std::string>());
    if (l.contains("favoritos") && l["favoritos"].is_array()) for (auto &x : l["favoritos"]) if (x.is_string()) g_favs.insert(x.get<std::string>());
    cargarCache();
    if (buscar) buscarAhora();
}

void buscarAhora() {
    if (!g_on || !g_meta.empty()) return;
    g_ultima = time(nullptr);
    for (const char *nm : {"catalogo.json", "catalogo.json.firma", "cuentas.json", "cuentas.json.firma"}) {
        Bajada b;
        b.dest = dir() + "/descarga/" + nm;
        if (!lanzar(b, sinCache(g_base + nm), {"--max-time", "30", "--max-filesize", "8000000"})) {
            g_estado = "No se encontró curl: no se puede abrir Source Play.";
            g_meta.clear();
            return;
        }
        g_meta.push_back(b);
    }
    g_estado = "Buscando juegos...";
}

void tick() {
    if (!g_on) return;
    if (!g_meta.empty()) {
        bool all = true;
        for (auto &b : g_meta) if (b.vivo && !sys::finished(b.p, &b.code)) all = false; else b.vivo = false;
        if (all) { procesarBusqueda(); g_meta.clear(); }
    }
    std::error_code ec;
    bool portada = false;
    for (auto it = g_portadas.begin(); it != g_portadas.end();) {
        if (it->vivo && !sys::finished(it->p)) { ++it; continue; }
        std::string fin = it->dest.substr(0, it->dest.size() - 8);
        if (huella(it->dest) == it->hash) { fs::rename(fs::u8path(it->dest), fs::u8path(fin), ec); portada = true; }
        else fs::remove(fs::u8path(it->dest), ec);
        it = g_portadas.erase(it);
    }
    if (portada) for (auto &j : g_juegos) if (j.portadaLocal.empty()) pedirPortada(j);
    // juegos: se comprueban tamano y huella; despues se guarda info.json (version, llave) y se cuenta la descarga
    for (auto it = g_juegosBaj.begin(); it != g_juegosBaj.end();) {
        if (it->vivo && !sys::finished(it->p, &it->code)) { ++it; continue; }
        const Juego *j = buscarJuego(it->id);
        long long sz = (long long)fs::file_size(fs::u8path(it->dest), ec);
        if (ec) sz = 0;
        if (j && sz == it->tam && huella(it->dest) == it->hash) {
            std::string fin = instDir(j->id) + "/juego.dat";
            fs::rename(fs::u8path(it->dest), fs::u8path(fin), ec);
            json info = {{"id", j->id}, {"nombre", j->nombre}, {"autor", j->autor}, {"version", j->version}, {"abierto", j->abierto}};
            if (!j->abierto) info["llave"] = j->llave;
            if (ec || !sys::writeDurable(instDir(j->id) + "/info.json", info.dump())) g_errores[it->id] = "No se pudo guardar el juego.";
            else { g_errores.erase(it->id); sumar(j->contador, j->id, 'd'); }
        } else {
            fs::remove(fs::u8path(it->dest), ec);
            g_errores[it->id] = it->code && it->code != 22 ? "No se pudo descargar. ¿Hay internet?" : "El archivo descargado no coincide con el catálogo y se borró.";
        }
        it = g_juegosBaj.erase(it);
    }
    for (auto it = g_fuentes.begin(); it != g_fuentes.end();) {
        if (it->vivo && !sys::finished(it->p, &it->code)) { ++it; continue; }
        if (huella(it->dest) == it->hash) fs::rename(fs::u8path(it->dest), fs::u8path(instDir(it->id) + "/original.dat"), ec);
        else { fs::remove(fs::u8path(it->dest), ec); g_errores[it->id] = "No se pudo descargar el proyecto original."; }
        it = g_fuentes.erase(it);
    }
    for (auto it = g_cuentas.begin(); it != g_cuentas.end();) {
        if (it->vivo && !sys::finished(it->p, &it->code)) { ++it; continue; }
        long long v = valor(*it);
        if (v >= 0) { if (it->hash == "d") g_cuenta[it->id].d = v; else g_cuenta[it->id].g = v; }
        it = g_cuentas.erase(it);
    }
    for (auto it = g_hits.begin(); it != g_hits.end();) {
        if (it->vivo && !sys::finished(it->p, &it->code)) { ++it; continue; }
        long long v = valor(*it);
        if (v >= 0) { if (it->hash == "d") g_cuenta[it->id].d = v; else g_cuenta[it->id].g = v; }
        it = g_hits.erase(it);
    }
    if (g_buscar && g_meta.empty() && time(nullptr) - g_ultima > kCada) buscarAhora();
}

std::string estado() { return g_estado; }
const std::vector<Juego> &juegos() { return g_juegos; }
std::vector<std::string> categorias() {
    std::vector<std::string> c;
    for (auto &j : g_juegos) if (std::find(c.begin(), c.end(), j.categoria) == c.end()) c.push_back(j.categoria);
    std::sort(c.begin(), c.end());
    return c;
}

int versionInstalada(const std::string &id) {
    std::error_code ec;
    if (!idSeguro(id) || !fs::exists(fs::u8path(instDir(id) + "/juego.dat"), ec)) return 0;
    json i = infoInstalado(id);
    return i.is_object() ? i.value("version", 0) : 0;
}
Estado estadoDe(const Juego &j) {
    for (auto &b : g_juegosBaj) if (b.id == j.id) return Estado::Descargando;
    int v = versionInstalada(j.id);
    if (!v) return Estado::NoInstalado;
    return v < j.version ? Estado::Actualizable : Estado::Instalado;
}
float progreso(const std::string &id) {
    for (auto &b : g_juegosBaj) if (b.id == id) {
        std::error_code ec;
        auto sz = fs::file_size(fs::u8path(b.dest), ec);
        return ec || b.tam <= 0 ? 0.0f : std::min(1.0f, (float)((double)sz / (double)b.tam));
    }
    return -1;
}
std::string error(const std::string &id) {
    auto it = g_errores.find(id);
    return it == g_errores.end() ? "" : it->second;
}
void descargar(const Juego &j) {
    if (!g_on || estadoDe(j) == Estado::Descargando || estadoDe(j) == Estado::Instalado) return;
    Bajada b; b.id = j.id; b.hash = j.juego.blake2b; b.tam = j.juego.tam;
    b.dest = instDir(j.id) + "/juego.dat.parcial";
    g_errores.erase(j.id);
    if (!lanzar(b, j.juego.url, {"--max-time", "600", "--max-filesize", std::to_string(j.juego.tam + 1)})) { g_errores[j.id] = "No se encontró curl."; return; }
    g_juegosBaj.push_back(b);
}
void borrar(const std::string &id) {
    if (!idSeguro(id)) return;
    std::error_code ec;
    fs::remove_all(fs::u8path(instDir(id)), ec);
    g_errores.erase(id);
}
bool leer(const std::string &id, json &doc, std::string &err) {
    if (!idSeguro(id)) { err = "Juego no válido"; return false; }
    std::string data = leer(instDir(id) + "/juego.dat");
    if (data.empty()) { err = "El juego no está descargado"; return false; }
    json info = infoInstalado(id);
    // la huella del catalogo, otra vez: por si el archivo cambio en el disco despues de descargarlo
    if (const Juego *j = buscarJuego(id); j && versionInstalada(id) == j->version && huella(instDir(id) + "/juego.dat") != j->juego.blake2b) {
        err = "El juego descargado está dañado: bórralo y descárgalo otra vez";
        return false;
    }
    std::string plain;
    if (cerrado::isPlay(data)) {
        if (!info.is_object() || !cerrado::unlockPlay(data, info.value("llave", ""), plain)) { err = "No se pudo abrir el juego cerrado"; return false; }
        data.swap(plain);
    }
    if (!exporter::sourceUnpack(data, doc, err)) {
        if (!plain.empty()) crypto_wipe(plain.data(), plain.size());
        return false;
    }
    return true;
}
std::vector<std::string> instaladosSinCatalogo() {
    std::vector<std::string> r;
    std::error_code ec;
    for (auto &e : fs::directory_iterator(fs::u8path(dir() + "/instalados"), ec)) {
        std::string id = e.path().filename().u8string();
        if (idSeguro(id) && !buscarJuego(id) && versionInstalada(id)) r.push_back(id);
    }
    return r;
}
std::string nombreInstalado(const std::string &id) {
    json i = infoInstalado(id);
    return i.is_object() ? i.value("nombre", id) : id;
}

void pedirFuente(const Juego &j) {
    if (!g_on || j.abierto || j.fuente.ruta.empty() || fuenteLista(j.id)) return;
    for (auto &b : g_fuentes) if (b.id == j.id) return;
    Bajada b; b.id = j.id; b.hash = j.fuente.blake2b; b.dest = instDir(j.id) + "/original.dat.parcial";
    if (lanzar(b, j.fuente.url, {"--max-time", "600", "--max-filesize", "200000000"})) g_fuentes.push_back(b);
}
bool fuenteLista(const std::string &id) {
    std::error_code ec;
    return idSeguro(id) && fs::exists(fs::u8path(instDir(id) + "/original.dat"), ec);
}
bool abrirFuente(const std::string &id, const std::string &clave, json &doc, std::string &err) {
    std::string data = leer(instDir(id) + "/original.dat"), plain;
    if (!cerrado::isLocked(data)) { err = "Falta el proyecto original"; return false; }
    if (!cerrado::unlockSource(data, clave, plain)) { err = "La clave no es correcta"; return false; }
    bool ok = exporter::sourceUnpack(plain, doc, err);
    crypto_wipe(plain.data(), plain.size());
    return ok;
}

void mirarContadores() {
    if (!g_on || !g_buscar || g_contador.empty() || !g_cuentas.empty() || reloj() - g_cuentasAt < kContadoresCada) return;
    g_cuentasAt = reloj();
    int n = 0;
    for (auto &j : g_juegos) {
        if (j.contador.empty() || n++ >= 40) continue;
        for (char q : {'d', 'g'}) {
            Bajada b; b.id = j.id; b.hash = std::string(1, q);
            b.dest = dir() + "/contadores/" + j.id + "-" + q + ".json";
            if (lanzar(b, urlContador("get", j.contador + "-" + q), {"--max-time", "15", "--max-filesize", "4096"})) g_cuentas.push_back(b);
        }
    }
}
long long descargas(const std::string &id) { auto it = g_cuenta.find(id); return it == g_cuenta.end() ? -1 : it->second.d; }
long long meGusta(const std::string &id) { auto it = g_cuenta.find(id); return it == g_cuenta.end() ? -1 : it->second.g; }
bool leGusta(const std::string &id) { return g_gustan.count(id) > 0; }
bool favorito(const std::string &id) { return g_favs.count(id) > 0; }
void setFavorito(const std::string &id, bool si) {
    if (si == favorito(id)) return;
    if (si) g_favs.insert(id); else g_favs.erase(id);
    guardarLocal();
}
void darMeGusta(const Juego &j) {
    if (g_gustan.count(j.id)) return;
    g_gustan.insert(j.id);
    guardarLocal();
    auto &c = g_cuenta[j.id];
    if (c.g >= 0) c.g++;
    sumar(j.contador, j.id, 'g');
}
int insignia(const std::string &login) {
    auto it = g_insignias.find(minus(login));
    return it == g_insignias.end() ? 0 : it->second;
}
std::vector<const Juego *> juegosDe(const std::string &login) {
    std::vector<const Juego *> r;
    for (auto &j : g_juegos) if (minus(j.autor) == minus(login)) r.push_back(&j);
    return r;
}
}  // namespace tienda
