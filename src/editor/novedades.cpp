#include "novedades.hpp"
#include "servidor.hpp"
#include "firma_publica.hpp"
#include "json.hpp"
#include "sys.hpp"
#include "ui.hpp"
#include "version.hpp"
#include "../../third_party/monocypher/monocypher.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace novedades {
namespace {
#ifdef _WIN32
const char *kSistema = "windows", *kExe = "Source2D.exe";
#else
const char *kSistema = "linux", *kExe = "Source2D";
#endif
const time_t kCada = 3 * 3600;
const double kSondeo = 5;
const double kVotosCada = 20;

struct Bajada { sys::Proc p; std::string dest, hash; bool vivo = false; int code = 0; };
struct Conteo {
    std::vector<long long> n;
    std::vector<Bajada> b;
    Bajada voto;
    double at = -1e9;
    std::string error;
};
struct DescargaVer { Version v; sys::Proc p; bool vivo = false; std::string parcial, final_, error; };

bool g_on = false, g_buscar = false, g_pruebas = false, g_hayCache = false;
std::string g_base, g_versDir, g_estado, g_contador, g_apiVotos;
uint8_t g_pub[32];
std::vector<Noticia> g_news;
std::vector<Version> g_vers;
std::set<int> g_leidas, g_vistas, g_nuevas, g_notificadas;
std::vector<Bajada> g_meta, g_imgs;
Bajada g_sondeo;
double g_sondeoAt = -1e9;
std::map<std::string, DescargaVer> g_desc;
std::map<int, Conteo> g_enc;
std::map<int, int> g_votos;
std::set<int> g_enviados;
time_t g_ultima = 0;

std::string dir() { return ui::configDir() + "/noticias"; }
std::string tmpDir() { return dir() + "/descarga"; }
double reloj() { return (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0; }
std::string sinCache(const std::string &url) {
    if (url.rfind("https://", 0) != 0) return url;
    return url + "?t=" + std::to_string((long long)time(nullptr));
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
std::string leer(const std::string &p) {
    std::ifstream in(fs::u8path(p), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
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
bool firmaValida(const std::string &data, const std::string &firma) {
    uint8_t sig[64];
    return fromHex(firma, sig, 64) && crypto_eddsa_check(sig, g_pub, (const uint8_t *)data.data(), data.size()) == 0;
}
json abrirFirmado(const std::string &data, const std::string &firma, const char *tipo) {
    if (data.empty() || !firmaValida(data, firma)) return nullptr;
    json j = json::parse(data, nullptr, false);
    if (!j.is_object() || j.value("tipo", "") != tipo || j.value("autor", "") != S2D_FIRMA_AUTOR) return nullptr;
    return j;
}
bool urlPermitida(const std::string &u) {
    if (u.rfind("https://", 0) == 0) return true;
    return g_pruebas && u.rfind("file://", 0) == 0;
}
bool rutaSegura(const std::string &s) {
    if (s.empty() || s.find("..") != std::string::npos || s[0] == '/') return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || strchr("_-./ ", c))) return false;
    return true;
}
std::string hoy() {
    tm t;
    sys::localTime(time(nullptr), t);
    char d[16];
    strftime(d, sizeof d, "%Y-%m-%d", &t);
    return d;
}

bool lanzarCurl(Bajada &b, const std::string &url, const std::vector<std::string> &extra) {
    std::vector<std::string> a = {"curl", "-fsL", "--proto", g_pruebas ? "=https,file" : "=https"};
    a.insert(a.end(), extra.begin(), extra.end());
    a.insert(a.end(), {"-o", b.dest, url});
    std::error_code ec;
    fs::remove(fs::u8path(b.dest), ec);
    b.vivo = sys::spawn(a, &b.p, true);
    return b.vivo;
}

void guardarLeidas() {
    if (g_pruebas && !getenv("S2D_NOVEDADES_URL")) return;
    json v = json::object();
    for (auto &kv : g_votos) v[std::to_string(kv.first)] = kv.second;
    json j = {{"leidas", g_leidas}, {"vistas", g_vistas}, {"notificadas", g_notificadas}, {"votos", v}, {"enviados", g_enviados}};
    sys::writeDurable(dir() + "/leidas.json", j.dump());
}

void cargarCache() {
    g_news.clear(); g_vers.clear(); g_hayCache = false;
    json n = abrirFirmado(leer(dir() + "/noticias.json"), leer(dir() + "/noticias.json.firma"), "noticias");
    json v = abrirFirmado(leer(dir() + "/versiones.json"), leer(dir() + "/versiones.json.firma"), "versiones");
    g_hayCache = !n.is_null() || !v.is_null();
    std::string today = hoy();
    if (n.is_object() && n["noticias"].is_array()) for (auto &x : n["noticias"]) {
        if (!x.is_object()) continue;
        std::string hasta = x.value("hasta", "");
        if (!hasta.empty() && hasta < today) continue;
        if (x.contains("para") && x["para"].is_array() && !x["para"].empty()) {
            bool mine = false;
            for (auto &p : x["para"]) if (p.is_string() && (p == S2D_VERSION || p == kSistema)) mine = true;
            if (!mine) continue;
        }
        Noticia o;
        o.id = x.value("id", 0); o.titulo = x.value("titulo", ""); o.texto = x.value("texto", ""); o.inferior = x.value("inferior", ""); o.tipo = x.value("tipo", "aviso");
        o.fecha = x.value("fecha", ""); o.version = x.value("version", ""); o.fijada = x.value("fijada", false); o.importante = x.value("importante", false);
        o.notificar = x.value("notificar", false);
        if (x.contains("encuesta") && x["encuesta"].is_object()) {
            auto &e = x["encuesta"];
            Encuesta en;
            en.pregunta = e.value("pregunta", ""); en.clave = e.value("clave", "");
            en.cierra = e.contains("cierra") && e["cierra"].is_number() ? e["cierra"].get<long long>() : 0;
            if (e.contains("opciones") && e["opciones"].is_array())
                for (auto &op : e["opciones"]) if (op.is_string() && en.opciones.size() < 10) en.opciones.push_back(op);
            bool ok = !en.clave.empty() && en.clave.size() <= 40 && en.opciones.size() >= 2;
            for (char c : en.clave) if (!(isalnum((unsigned char)c) || c == '-')) ok = false;
            if (ok) { o.hayEncuesta = true; o.encuesta = en; }
        }
        std::string en = x.value("enlace", "");
        if (en.rfind("https://", 0) == 0) o.enlace = en;
        if (x.contains("imagen") && x["imagen"].is_object()) {
            std::string arch = x["imagen"].value("archivo", ""), h = x["imagen"].value("blake2b", "");
            uint8_t tmp[32];
            if (rutaSegura(arch) && fromHex(h, tmp, 32)) {
                std::string local = dir() + "/imagenes/" + h + fs::u8path(arch).extension().u8string();
                std::error_code ec;
                if (fs::exists(fs::u8path(local), ec)) o.imagen = local;
                else if (g_buscar) {
                    bool ya = false;
                    for (auto &b : g_imgs) if (b.hash == h) ya = true;
                    if (!ya) {
                        fs::create_directories(fs::u8path(dir() + "/imagenes"), ec);
                        Bajada b; b.dest = local + ".parcial"; b.hash = h;
                        if (lanzarCurl(b, g_base + arch, {"--max-time", "60", "--max-filesize", "8000000"})) g_imgs.push_back(b);
                    }
                }
            }
        }
        g_news.push_back(o);
    }
    std::sort(g_news.begin(), g_news.end(), [](const Noticia &a, const Noticia &b) {
        if (a.fijada != b.fijada) return a.fijada;
        if (a.fecha != b.fecha) return a.fecha > b.fecha;
        return a.id > b.id;
    });
    if (v.is_object() && v["versiones"].is_array()) for (auto &x : v["versiones"]) {
        if (!x.is_object() || !x.contains("archivos") || !x["archivos"].is_object() || !x["archivos"].contains(kSistema)) continue;
        auto &f = x["archivos"][kSistema];
        Version o;
        o.nombre = x.value("nombre", ""); o.num = x.value("num", 0); o.canal = x.value("canal", "estable");
        o.fecha = x.value("fecha", ""); o.notas = x.value("notas", "");
        if (!f.is_object()) continue;
        o.url = f.value("url", ""); o.blake2b = f.value("blake2b", ""); o.tam = f.value("tam", 0LL);
        uint8_t tmp[32];
        if (!rutaSegura(o.nombre) || !urlPermitida(o.url) || !fromHex(o.blake2b, tmp, 32) || o.tam <= 0) continue;
        g_vers.push_back(o);
    }
    std::sort(g_vers.begin(), g_vers.end(), [](const Version &a, const Version &b) { return a.num > b.num; });
}

void procesarBusqueda() {
    int ok = 0, falta = 0;
    bool conectado = false;
    for (auto &b : g_meta) if (b.code == 0 || b.code == 22) conectado = true;
    for (const char *nm : {"noticias.json", "versiones.json"}) {
        std::string d = leer(tmpDir() + "/" + nm), f = leer(tmpDir() + "/" + nm + ".firma");
        if (d.empty() || f.empty()) { falta++; continue; }
        if (abrirFirmado(d, f, nm[0] == 'n' ? "noticias" : "versiones").is_null()) {
            fprintf(stderr, "Source 2D: %s no viene firmado por " S2D_FIRMA_AUTOR ", se ignora\n", nm);
            continue;
        }
        sys::writeDurable(dir() + "/" + nm, d);
        sys::writeDurable(dir() + "/" + nm + ".firma", f);
        ok++;
    }
    std::error_code ec;
    fs::remove_all(fs::u8path(tmpDir()), ec);
    cargarCache();
    if (ok == 2) g_estado.clear();
    else if (falta == 2 && conectado) g_estado = g_hayCache ? "" : "Todavía no hay noticias publicadas.";
    else if (falta == 2) g_estado = g_hayCache ? "Sin conexión: se ven las últimas noticias descargadas." : "Sin conexión. Las noticias saldrán aquí cuando haya internet.";
    else g_estado = "Algunas noticias no se pudieron comprobar y no se muestran.";
}
}

void iniciar(bool buscar, const std::string &versionsDir, bool pruebas) {
    g_versDir = versionsDir;
    g_pruebas = pruebas;
    std::string pub = S2D_FIRMA_PUBLICA;
    const char *url = getenv("S2D_NOVEDADES_URL");
    if (pruebas) {
        const char *k = getenv("S2D_FIRMA_PUBLICA");
        pub = k ? k : "";
        if (!url) return;
    }
    g_base = url && *url ? std::string(url) : servidor::r2() + "novedades/";
    g_contador = pruebas && !getenv("S2D_R2_URL") ? "" : servidor::r2();
    g_apiVotos = pruebas && !getenv("S2D_API_URL") ? "" : servidor::api();
    if (!fromHex(pub, g_pub, 32)) return;
    g_on = true;
    g_buscar = buscar;
    if (pruebas) {
        std::error_code ec;
        fs::remove_all(fs::u8path(dir()), ec);
    }
    json j = json::parse(leer(dir() + "/leidas.json"), nullptr, false);
    if (j.is_object()) {
        for (auto &x : j.value("leidas", json::array())) if (x.is_number_integer()) g_leidas.insert(x.get<int>());
        for (auto &x : j.value("vistas", json::array())) if (x.is_number_integer()) g_vistas.insert(x.get<int>());
        for (auto &x : j.value("notificadas", json::array())) if (x.is_number_integer()) g_notificadas.insert(x.get<int>());
        if (j.contains("votos") && j["votos"].is_object())
            for (auto &kv : j["votos"].items()) if (kv.value().is_number_integer()) g_votos[atoi(kv.key().c_str())] = kv.value().get<int>();
        for (auto &x : j.value("enviados", json::array())) if (x.is_number_integer()) g_enviados.insert(x.get<int>());
    }
    cargarCache();
    if (buscar) buscarAhora();
}

void buscarAhora() {
    if (!g_on || !g_meta.empty()) return;
    g_ultima = time(nullptr);
    std::error_code ec;
    fs::create_directories(fs::u8path(tmpDir()), ec);
    for (const char *nm : {"noticias.json", "noticias.json.firma", "versiones.json", "versiones.json.firma"}) {
        Bajada b;
        b.dest = tmpDir() + "/" + nm;
        if (!lanzarCurl(b, sinCache(g_base + nm), {"--max-time", "30", "--max-filesize", "2000000"})) {
            g_estado = "No se encontró curl: no se pueden buscar noticias.";
            g_meta.clear();
            return;
        }
        g_meta.push_back(b);
    }
    g_estado = "Buscando noticias...";
}

void tick() {
    if (!g_on) return;
    if (!g_meta.empty()) {
        bool all = true;
        for (auto &b : g_meta) if (b.vivo && !sys::finished(b.p, &b.code)) all = false; else b.vivo = false;
        if (all) { procesarBusqueda(); g_meta.clear(); }
    }
    bool img = false;
    for (auto it = g_imgs.begin(); it != g_imgs.end();) {
        if (it->vivo && !sys::finished(it->p)) { ++it; continue; }
        std::error_code ec;
        std::string fin = it->dest.substr(0, it->dest.size() - 8);
        if (huella(it->dest) == it->hash) { fs::rename(fs::u8path(it->dest), fs::u8path(fin), ec); img = true; }
        else fs::remove(fs::u8path(it->dest), ec);
        it = g_imgs.erase(it);
    }
    if (img) cargarCache();
    for (auto &kv : g_desc) {
        DescargaVer &d = kv.second;
        if (!d.vivo || !sys::finished(d.p)) continue;
        d.vivo = false;
        std::error_code ec;
        long long sz = (long long)fs::file_size(fs::u8path(d.parcial), ec);
        if (ec) sz = 0;
        if (sz == d.v.tam && huella(d.parcial) == d.v.blake2b) {
            fs::rename(fs::u8path(d.parcial), fs::u8path(d.final_), ec);
            if (ec) d.error = "No se pudo guardar la versión descargada.";
            else sys::setExecutable(d.final_);
        } else if (sz < d.v.tam) d.error = "Se cortó la descarga. Pulsa otra vez para seguir donde se quedó.";
        else { fs::remove(fs::u8path(d.parcial), ec); d.error = "El archivo descargado no coincide con la firma y se borró."; }
    }
    auto valor = [](Bajada &b) -> long long {
        json j = json::parse(leer(b.dest), nullptr, false);
        std::error_code ec;
        fs::remove(fs::u8path(b.dest), ec);
        if (b.code == 22 || (g_pruebas && b.code == 37)) return 0;
        if (b.code != 0 || !j.is_object() || !j.contains("value") || !j["value"].is_number_integer()) return -1;
        return j["value"].get<long long>();
    };
    for (auto &kv : g_enc) {
        Conteo &c = kv.second;
        for (size_t i = 0; i < c.b.size(); i++) {
            Bajada &b = c.b[i];
            if (!b.vivo || !sys::finished(b.p, &b.code)) continue;
            b.vivo = false;
            long long v = valor(b);
            if (v >= 0 && i < c.n.size()) c.n[i] = v;
            if (v < 0) c.error = "No se pudieron leer los votos.";
        }
        if (c.voto.vivo && sys::finished(c.voto.p, &c.voto.code)) {
            c.voto.vivo = false;
            long long v = valor(c.voto);
            auto it = g_votos.find(kv.first);
            if (v < 0) c.error = "No se pudo enviar el voto: se volverá a intentar.";
            else {
                if (it != g_votos.end() && it->second >= 0 && it->second < (int)c.n.size()) c.n[it->second] = v;
                c.error.clear();
                g_enviados.insert(kv.first);
                guardarLeidas();
            }
        }
    }
    if (g_buscar && g_meta.empty() && time(nullptr) - g_ultima > kCada) buscarAhora();
    if (g_sondeo.vivo) {
        if (sys::finished(g_sondeo.p, &g_sondeo.code)) {
            g_sondeo.vivo = false;
            std::string nueva = leer(g_sondeo.dest), vieja = leer(dir() + "/noticias.json.firma");
            auto limpio = [](std::string t) { while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back(); return t; };
            if (g_sondeo.code == 0 && !limpio(nueva).empty() && limpio(nueva) != limpio(vieja) && g_meta.empty()) buscarAhora();
            std::error_code ec;
            fs::remove(fs::u8path(g_sondeo.dest), ec);
        }
    } else if (g_buscar && g_meta.empty() && reloj() - g_sondeoAt >= kSondeo) {
        g_sondeoAt = reloj();
        std::error_code ec;
        fs::create_directories(fs::u8path(dir()), ec);
        g_sondeo = Bajada{};
        g_sondeo.dest = dir() + "/sondeo.firma";
        lanzarCurl(g_sondeo, sinCache(g_base + "noticias.json.firma"), {"--max-time", "4", "--max-filesize", "4096"});
    }
}

void setBuscar(bool on) {
    g_buscar = on;
    if (on) buscarAhora();
}
bool activo() { return g_on; }
std::string estado() { return g_estado; }
const std::vector<Noticia> &noticias() { return g_news; }
const std::vector<Version> &versiones() { return g_vers; }
int sinLeer() {
    int n = 0;
    for (auto &x : g_news) if (!g_leidas.count(x.id)) n++;
    return n;
}
void marcarLeidas() {
    bool cambio = false;
    for (auto &x : g_news) if (!g_leidas.count(x.id)) { g_nuevas.insert(x.id); g_leidas.insert(x.id); cambio = true; }
    if (cambio) guardarLeidas();
}
bool nueva(int id) { return g_nuevas.count(id) > 0; }
const Noticia *importantePendiente() {
    for (auto &x : g_news) if (x.importante && !g_vistas.count(x.id)) return &x;
    return nullptr;
}
const Noticia *notificacionPendiente() {
    for (auto &x : g_news) if (x.notificar && !g_notificadas.count(x.id) && !g_leidas.count(x.id)) return &x;
    return nullptr;
}
void notificada(int id) {
    g_notificadas.insert(id);
    guardarLeidas();
}
void importanteVisto(int id) {
    g_vistas.insert(id);
    if (!g_leidas.count(id)) { g_nuevas.insert(id); g_leidas.insert(id); }
    guardarLeidas();
}

namespace {
std::string urlVoto(const Encuesta &e, int i) {
    std::string u = g_contador + "contadores/" + e.clave + "-" + std::to_string(i) + ".json";
    return u.rfind("https://", 0) == 0 ? u + "?t=" + std::to_string((long long)time(nullptr)) : u;
}
void enviarVoto(const Noticia &n, Conteo &c) {
    auto it = g_votos.find(n.id);
    if (it == g_votos.end() || c.voto.vivo || g_apiVotos.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::u8path(dir() + "/encuestas"), ec);
    c.voto = Bajada{};
    c.voto.dest = dir() + "/encuestas/" + std::to_string(n.id) + "-voto.json";
    lanzarCurl(c.voto, g_apiVotos + "/contador/" + n.encuesta.clave + "-" + std::to_string(it->second), {"--max-time", "15", "--max-filesize", "4096", "-X", "POST"});
}
}

bool encuestaCerrada(const Noticia &n) { return n.encuesta.cierra > 0 && (long long)time(nullptr) >= n.encuesta.cierra; }

void mirarEncuesta(const Noticia &n) {
    if (!g_on || !n.hayEncuesta) return;
    Conteo &c = g_enc[n.id];
    if (c.n.size() != n.encuesta.opciones.size()) c.n.assign(n.encuesta.opciones.size(), -1);
    if (!g_buscar || g_contador.empty() || c.voto.vivo || reloj() - c.at < kVotosCada) return;
    for (auto &b : c.b) if (b.vivo) return;
    c.at = reloj();
    if (g_votos.count(n.id) && !g_enviados.count(n.id) && !encuestaCerrada(n)) { enviarVoto(n, c); return; }
    std::error_code ec;
    fs::create_directories(fs::u8path(dir() + "/encuestas"), ec);
    c.b.assign(n.encuesta.opciones.size(), Bajada{});
    for (size_t i = 0; i < c.b.size(); i++) {
        c.b[i].dest = dir() + "/encuestas/" + std::to_string(n.id) + "-" + std::to_string(i) + ".json";
        lanzarCurl(c.b[i], urlVoto(n.encuesta, (int)i), {"--max-time", "15", "--max-filesize", "4096"});
    }
}
std::vector<long long> votos(int id) {
    auto it = g_enc.find(id);
    return it == g_enc.end() ? std::vector<long long>() : it->second.n;
}
int miVoto(int id) {
    auto it = g_votos.find(id);
    return it == g_votos.end() ? -1 : it->second;
}
void votar(const Noticia &n, int opcion) {
    if (!g_on || !n.hayEncuesta || g_votos.count(n.id) || opcion < 0 || opcion >= (int)n.encuesta.opciones.size() || encuestaCerrada(n)) return;
    g_votos[n.id] = opcion;
    guardarLeidas();
    Conteo &c = g_enc[n.id];
    if (c.n.size() != n.encuesta.opciones.size()) c.n.assign(n.encuesta.opciones.size(), -1);
    if (c.n[opcion] >= 0) c.n[opcion]++;
    if (g_buscar) enviarVoto(n, c);
    else c.error = "Buscar noticias está apagado en Opciones: el voto se enviará al activarlo.";
}
std::string errorEncuesta(int id) {
    auto it = g_enc.find(id);
    return it == g_enc.end() ? "" : it->second.error;
}

void descargar(const Version &v) {
    if (!g_on || instalada(v.nombre)) return;
    DescargaVer &d = g_desc[v.nombre];
    if (d.vivo) return;
    d.v = v;
    d.error.clear();
    std::string carpeta = g_versDir + "/" + v.nombre;
    std::error_code ec;
    fs::create_directories(fs::u8path(carpeta), ec);
    d.final_ = carpeta + "/" + kExe;
    d.parcial = d.final_ + ".parcial";
    std::vector<std::string> a = {"curl", "-fsL", "--proto", g_pruebas ? "=https,file" : "=https", "--proto-redir", "=https",
                                  "-C", "-", "--speed-limit", "1", "--speed-time", "60", "-o", d.parcial, v.url};
    d.vivo = sys::spawn(a, &d.p, true);
    if (!d.vivo) d.error = "No se encontró curl.";
}
float progreso(const std::string &nombre) {
    auto it = g_desc.find(nombre);
    if (it == g_desc.end() || !it->second.vivo) return -1;
    std::error_code ec;
    auto sz = fs::file_size(fs::u8path(it->second.parcial), ec);
    return ec ? 0.0f : std::min(1.0f, (float)((double)sz / (double)it->second.v.tam));
}
std::string errorDescarga(const std::string &nombre) {
    auto it = g_desc.find(nombre);
    return it == g_desc.end() ? "" : it->second.error;
}
bool instalada(const std::string &nombre) {
    if (nombre == S2D_VERSION) return true;
    std::error_code ec;
    return fs::exists(fs::u8path(g_versDir + "/" + nombre + "/" + kExe), ec);
}
}
