// Source 2D - enviar juegos a Source Play (ver envio.hpp).
// Cada llamada a GitHub es un curl en segundo plano: el cuerpo va en un archivo (--data-binary @) y las cabeceras con el
// token tambien (-H @), asi el token no aparece en la lista de procesos. La respuesta se lee de su archivo y el estado
// HTTP de las cabeceras guardadas (-D).
#include "envio.hpp"
#include "sys.hpp"
#include "ui.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

namespace envio {
namespace {
// OAuth App "Source 2D" de FloppaKFC (github.com/settings/developers, con "Enable Device Flow"). Es publico: no es un
// secreto. Mientras este vacio, "Entrar con GitHub" explica que falta.
const char *kClientId = "Ov23lipmhcQcBtNRYcEX";
const char *kDueno = "FloppaKFC", *kRepo = "source2d-envios", *kRepoJuegos = "source2d-juegos";

bool g_pruebas = false;
std::string g_api = "https://api.github.com", g_web = "https://github.com", g_clientId = kClientId;
std::string g_token, g_user, g_errCuenta, g_codigo, g_urlCodigo, g_deviceCode, g_estado;
bool g_entrando = false, g_enviando = false, g_fallo = false;
double g_intervalo = 5, g_proximoSondeo = 0, g_caduca = 0, g_misAt = -1e9;
std::vector<Mio> g_mios;

std::string dir() { return ui::configDir(); }
double reloj() { return (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0; }
std::string leer(const std::string &p) {
    std::ifstream in(fs::u8path(p), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
// archivo solo para este usuario del sistema (token, cabeceras)
bool escribirPrivado(const std::string &p, const std::string &data) {
    std::error_code ec;
    fs::create_directories(fs::u8path(p).parent_path(), ec);
    { std::ofstream o(fs::u8path(p), std::ios::binary | std::ios::trunc); }
    fs::permissions(fs::u8path(p), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    std::ofstream o(fs::u8path(p), std::ios::binary | std::ios::trunc);
    return (bool)(o << data);
}
std::string base64(const std::string &in) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i < in.size()) {
        unsigned v = (unsigned char)in[i] << 16 | (i + 1 < in.size() ? (unsigned char)in[i + 1] << 8 : 0);
        o += t[v >> 18]; o += t[(v >> 12) & 63];
        o += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
        o += '=';
    }
    return o;
}
std::string urlEnc(const std::string &s) {
    std::string e;
    for (unsigned char c : s) {
        if (isalnum(c) || strchr("-_.~", c)) e += (char)c;
        else { char h[4]; snprintf(h, sizeof h, "%%%02X", c); e += h; }
    }
    return e;
}

// una peticion a GitHub; al acabar se llama a fin(estado HTTP, cuerpo como JSON). Estado 0: curl no llego
struct Pet {
    std::string metodo, url, cuerpo;
    bool form = false, conToken = true;
    std::function<void(int, const json &)> fin;
    bool estrellas = false;   // Accept: application/vnd.github.star+json (fechas de las estrellas)
};
std::deque<Pet> g_cola;
bool g_activa = false;
sys::Proc g_proc;
Pet g_actual;

std::string tmp(const char *nm) { return dir() + "/github-" + nm; }
void lanzarSiguiente() {
    if (g_activa || g_cola.empty()) return;
    g_actual = std::move(g_cola.front());
    g_cola.pop_front();
    std::string hdr = "Accept: application/json\nUser-Agent: Source2D\n";
    if (!g_actual.form) hdr = std::string("Accept: application/vnd.github") + (g_actual.estrellas ? ".star" : "") + "+json\nX-GitHub-Api-Version: 2022-11-28\nUser-Agent: Source2D\nContent-Type: application/json\n";
    if (g_actual.conToken && !g_token.empty()) hdr += "Authorization: Bearer " + g_token + "\n";
    escribirPrivado(tmp("cabeceras"), hdr);
    std::error_code ec;
    fs::remove(fs::u8path(tmp("respuesta")), ec);
    fs::remove(fs::u8path(tmp("estado")), ec);
    std::vector<std::string> a = {"curl", "-sL", "--proto", g_pruebas ? "=https,http,file" : "=https", "--max-time", "120",
                                  "-H", "@" + tmp("cabeceras"), "-D", tmp("estado"), "-o", tmp("respuesta"), "-X", g_actual.metodo};
    if (!g_actual.cuerpo.empty()) {
        escribirPrivado(tmp("cuerpo"), g_actual.cuerpo);
        a.insert(a.end(), {"--data-binary", "@" + tmp("cuerpo")});
    }
    a.push_back(g_actual.url);
    g_activa = sys::spawn(a, &g_proc, true);
    if (!g_activa) { auto f = g_actual.fin; if (f) f(0, json()); }
}
void pedir(Pet p) { g_cola.push_back(std::move(p)); lanzarSiguiente(); }
void terminarPeticion() {
    g_activa = false;
    std::string st = leer(tmp("estado"));
    int code = 0;
    // con redirecciones hay varias lineas "HTTP/x NNN": vale la ultima (solo las que empiezan asi: "Server:
    // BaseHTTP/0.6" tambien lleva "HTTP/")
    for (size_t p = 0; p < st.size(); p = st.find('\n', p) == std::string::npos ? st.size() : st.find('\n', p) + 1) {
        if (st.compare(p, 5, "HTTP/") != 0) continue;
        size_t sp = st.find(' ', p);
        if (sp != std::string::npos) code = atoi(st.c_str() + sp + 1);
    }
    json body = json::parse(leer(tmp("respuesta")), nullptr, false);
    std::error_code ec;
    for (const char *nm : {"cabeceras", "cuerpo", "respuesta", "estado"}) fs::remove(fs::u8path(tmp(nm)), ec);
    auto f = g_actual.fin;
    g_actual = Pet{};
    if (f) f(code, body);
}

void guardarCuenta() {
    if (g_token.empty()) { std::error_code ec; fs::remove(fs::u8path(dir() + "/github.json"), ec); return; }
    escribirPrivado(dir() + "/github.json", json{{"token", g_token}, {"usuario", g_user}}.dump());
}
json misEnvios() {
    json j = json::parse(leer(dir() + "/github-envios.json"), nullptr, false);
    return j.is_array() ? j : json::array();
}
void cargarMios() {
    g_mios.clear();
    for (auto &x : misEnvios()) {
        if (!x.is_object() || x.value("usuario", "") != g_user) continue;
        Mio m;
        m.numero = x.value("numero", 0); m.nombre = x.value("nombre", ""); m.fecha = x.value("fecha", "");
        m.estado = x.value("estado", "revisando"); m.motivo = x.value("motivo", ""); m.url = x.value("url", "");
        g_mios.push_back(m);
    }
}
void guardarMio(const Mio &m) {
    json all = misEnvios(), out = json::array();
    for (auto &x : all) if (!(x.is_object() && x.value("numero", 0) == m.numero && x.value("usuario", "") == g_user)) out.push_back(x);
    out.push_back({{"numero", m.numero}, {"nombre", m.nombre}, {"fecha", m.fecha}, {"estado", m.estado}, {"motivo", m.motivo}, {"url", m.url}, {"usuario", g_user}});
    sys::writeDurable(dir() + "/github-envios.json", out.dump());
}

// registro en Source Play: una estrella en el repositorio de juegos (PUT es idempotente: repetirlo no cambia nada)
void registrar() {
    pedir({"PUT", g_api + "/user/starred/" + kDueno + "/" + kRepoJuegos, "", false, true, nullptr});
}
void pedirUsuario() {
    pedir({"GET", g_api + "/user", "", false, true, [](int code, const json &b) {
        if (code == 200 && b.is_object() && b.contains("login")) { g_user = b["login"].get<std::string>(); g_errCuenta.clear(); guardarCuenta(); cargarMios(); registrar(); }
        else if (code == 401) { g_token.clear(); g_user.clear(); guardarCuenta(); g_errCuenta = "GitHub ya no acepta la sesión: vuelve a entrar."; }
        else if (code == 0) g_errCuenta = "Sin conexión con GitHub.";
    }});
}
void sondearToken() {
    std::string cuerpo = "client_id=" + urlEnc(g_clientId) + "&device_code=" + urlEnc(g_deviceCode) +
                         "&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code";
    pedir({"POST", g_web + "/login/oauth/access_token", cuerpo, true, false, [](int code, const json &b) {
        if (!g_entrando) return;
        std::string err = b.is_object() ? b.value("error", "") : "";
        if (code == 200 && b.is_object() && b.contains("access_token")) {
            g_token = b["access_token"].get<std::string>();
            g_entrando = false; g_codigo.clear();
            pedirUsuario();
        } else if (err == "authorization_pending" || code == 0) g_proximoSondeo = reloj() + g_intervalo;
        else if (err == "slow_down") { g_intervalo += 5; g_proximoSondeo = reloj() + g_intervalo; }
        else {
            g_entrando = false; g_codigo.clear();
            g_errCuenta = err == "access_denied" ? "Cancelaste la entrada en GitHub." : err == "expired_token" ? "El código caducó: vuelve a pulsar Entrar." : "GitHub no dejó entrar (" + (err.empty() ? std::to_string(code) : err) + ").";
        }
    }});
}

// pasos del envio
struct Envio { Paquete p; std::string rama, sha; int intentos = 0; std::vector<std::string> pendientes; };
Envio g_env;
void fallar(const std::string &msg) { g_enviando = false; g_fallo = true; g_estado = msg; }
void abrirPR();
void subirSiguiente() {
    if (g_env.pendientes.empty()) { abrirPR(); return; }
    std::string nm = g_env.pendientes.back();
    g_env.pendientes.pop_back();
    g_estado = "Subiendo " + nm + "...";
    std::string ruta = "envios/" + g_user + "/" + g_env.p.id + "/" + nm;
    json c = {{"message", "Envío: " + g_env.p.nombre + " (" + nm + ")"}, {"content", base64(g_env.p.archivos[nm])}, {"branch", g_env.rama}};
    pedir({"PUT", g_api + "/repos/" + g_user + "/" + kRepo + "/contents/" + ruta, c.dump(), false, true, [nm](int code, const json &b) {
        if (code == 201 || code == 200) subirSiguiente();
        else fallar("No se pudo subir " + nm + " (" + std::to_string(code) + (b.is_object() ? " " + b.value("message", "") : "") + ").");
    }});
}
void crearRama() {
    g_estado = "Preparando el envío...";
    json c = {{"ref", "refs/heads/" + g_env.rama}, {"sha", g_env.sha}};
    pedir({"POST", g_api + "/repos/" + g_user + "/" + kRepo + "/git/refs", c.dump(), false, true, [](int code, const json &b) {
        if (code != 201) { fallar("No se pudo crear la rama del envío (" + std::to_string(code) + (b.is_object() ? " " + b.value("message", "") : "") + ")."); return; }
        g_env.pendientes.clear();
        for (auto &kv : g_env.p.archivos) g_env.pendientes.push_back(kv.first);
        subirSiguiente();
    }});
}
void esperarCopia() {
    // la copia (fork) tarda unos segundos en existir: se pregunta por su rama main hasta que responde
    pedir({"GET", g_api + "/repos/" + g_user + "/" + kRepo + "/git/ref/heads/main", "", false, true, [](int code, const json &b) {
        if (code == 200 && b.is_object() && b.contains("object")) { g_env.sha = b["object"].value("sha", ""); crearRama(); return; }
        if (++g_env.intentos > 30) { fallar("GitHub tardó demasiado en crear tu copia del repositorio. Prueba otra vez en un rato."); return; }
        g_estado = "Esperando a GitHub...";
        g_proximoSondeo = reloj() + 2;   // lo relanza tick()
    }});
}
void abrirPR() {
    g_estado = "Enviando a revisión...";
    json c = {{"title", "Envío: " + g_env.p.nombre}, {"head", g_user + ":" + g_env.rama}, {"base", "main"},
              {"body", "Juego enviado desde el editor de Source 2D por @" + g_user + ".\n\nCarpeta: `envios/" + g_user + "/" + g_env.p.id + "/`"},
              {"maintainer_can_modify", true}};
    pedir({"POST", g_api + "/repos/" + kDueno + "/" + kRepo + "/pulls", c.dump(), false, true, [](int code, const json &b) {
        if (code != 201 || !b.is_object()) { fallar("No se pudo enviar a revisión (" + std::to_string(code) + (b.is_object() ? " " + b.value("message", "") : "") + ")."); return; }
        Mio m;
        m.numero = b.value("number", 0); m.nombre = g_env.p.nombre; m.url = b.value("html_url", ""); m.estado = "revisando";
        char d[16]; time_t t = time(nullptr); tm lt; sys::localTime(t, lt); strftime(d, sizeof d, "%Y-%m-%d", &lt);
        m.fecha = d;
        guardarMio(m);
        cargarMios();
        g_enviando = false; g_fallo = false;
        g_estado = "¡Enviado! Ahora lo revisa Source Play. Aquí verás si se aprueba.";
        g_env = Envio{};
    }});
}
}  // namespace

void iniciar(bool pruebas) {
    g_pruebas = pruebas;
    if (const char *a = getenv("S2D_GITHUB_API")) g_api = a;
    if (const char *w = getenv("S2D_GITHUB_WEB")) g_web = w;
    if (const char *c = getenv("S2D_GITHUB_CLIENT_ID")) g_clientId = c;
    if (pruebas && !getenv("S2D_GITHUB_API")) return;   // las pruebas no tocan GitHub de verdad
    json j = json::parse(leer(dir() + "/github.json"), nullptr, false);
    if (j.is_object()) { g_token = j.value("token", ""); g_user = j.value("usuario", ""); }
    if (!g_token.empty()) { cargarMios(); pedirUsuario(); }
}

namespace {
std::map<std::string, Perfil> g_perfiles;
std::vector<std::pair<std::string, std::string>> g_registrados;
double g_registradosAt = -1e9;
struct Avatar { sys::Proc p; std::string login, dest; };
std::vector<Avatar> g_avatares;
bool loginSeguro(const std::string &s) {
    if (s.empty() || s.size() > 39) return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || c == '-')) return false;
    return true;
}
std::vector<std::string> logins(const json &b) {
    std::vector<std::string> r;
    if (b.is_array()) for (auto &x : b) if (x.is_object() && x.contains("login") && x["login"].is_string()) r.push_back(x["login"]);
    return r;
}
}  // namespace

void pedirPerfil(const std::string &login) {
    if (!loginSeguro(login) || g_perfiles.count(login)) return;
    Perfil &p = g_perfiles[login];
    p.login = login;
    pedir({"GET", g_api + "/users/" + login, "", false, true, [login](int code, const json &b) {
        Perfil &p = g_perfiles[login];
        if (code != 200 || !b.is_object()) { p.error = true; return; }
        p.cargado = true;
        p.nombre = b.contains("name") && b["name"].is_string() ? b["name"].get<std::string>() : "";
        p.bio = b.contains("bio") && b["bio"].is_string() ? b["bio"].get<std::string>() : "";
        p.creado = b.contains("created_at") && b["created_at"].is_string() ? b["created_at"].get<std::string>().substr(0, 10) : "";
        p.seguidores = b.value("followers", 0); p.siguiendo = b.value("following", 0);
        std::string av = b.contains("avatar_url") && b["avatar_url"].is_string() ? b["avatar_url"].get<std::string>() : "";
        if (av.rfind("https://", 0) == 0 || (g_pruebas && av.rfind("file://", 0) == 0)) {
            Avatar a; a.login = login; a.dest = dir() + "/avatares/" + login + ".img";
            std::error_code ec;
            fs::create_directories(fs::u8path(a.dest).parent_path(), ec);
            std::string url = av + (av.find('?') == std::string::npos ? "?s=160" : "&s=160");
            if (sys::spawn({"curl", "-fsL", "--proto", g_pruebas ? "=https,file" : "=https", "--max-time", "30", "--max-filesize", "2000000", "-o", a.dest, url}, &a.p, true))
                g_avatares.push_back(a);
        }
    }});
    if (conectado() && login != g_user)
        pedir({"GET", g_api + "/user/following/" + login, "", false, true, [login](int code, const json &) {
            g_perfiles[login].sigo = code == 204 ? 1 : code == 404 ? 0 : -1;
        }});
}
const Perfil *perfil(const std::string &login) {
    auto it = g_perfiles.find(login);
    return it == g_perfiles.end() ? nullptr : &it->second;
}
void pedirListas(const std::string &login) {
    auto it = g_perfiles.find(login);
    if (it == g_perfiles.end() || it->second.listas) return;
    it->second.listas = true;
    pedir({"GET", g_api + "/users/" + login + "/followers?per_page=100", "", false, true, [login](int code, const json &b) {
        if (code == 200) g_perfiles[login].listaSeguidores = logins(b);
    }});
    pedir({"GET", g_api + "/users/" + login + "/following?per_page=100", "", false, true, [login](int code, const json &b) {
        if (code == 200) g_perfiles[login].listaSiguiendo = logins(b);
    }});
}
void seguir(const std::string &login, bool si) {
    if (!conectado() || !loginSeguro(login) || login == g_user) return;
    pedir({si ? "PUT" : "DELETE", g_api + "/user/following/" + login, "", false, true, [login, si](int code, const json &) {
        if (code != 204) return;
        Perfil &p = g_perfiles[login];
        if (p.sigo != (si ? 1 : 0) && p.seguidores >= 0) p.seguidores += si ? 1 : -1;
        p.sigo = si ? 1 : 0;
        p.listas = false; p.listaSeguidores.clear(); p.listaSiguiendo.clear();
        auto yo = g_perfiles.find(g_user);   // y mis "siguiendo"
        if (yo != g_perfiles.end() && yo->second.siguiendo >= 0) { yo->second.siguiendo += si ? 1 : -1; yo->second.listas = false; }
    }});
}
void pedirRegistrados() {
    if (reloj() - g_registradosAt < (g_pruebas ? 1 : 120)) return;
    g_registradosAt = reloj();
    // con la cabecera star+json cada estrella trae su fecha (starred_at)
    Pet p{"GET", g_api + "/repos/" + kDueno + "/" + kRepoJuegos + "/stargazers?per_page=100", "", false, true, [](int code, const json &b) {
        if (code != 200 || !b.is_array()) return;
        g_registrados.clear();
        for (auto &x : b) {
            if (!x.is_object() || !x.contains("user") || !x["user"].is_object()) continue;
            std::string l = x["user"].value("login", ""), f = x.value("starred_at", "");
            if (loginSeguro(l)) g_registrados.push_back({l, f.substr(0, 10)});
        }
        std::sort(g_registrados.begin(), g_registrados.end(), [](auto &a, auto &b) { return a.second > b.second; });
    }};
    p.estrellas = true;
    pedir(p);
}
const std::vector<std::pair<std::string, std::string>> &registrados() { return g_registrados; }
std::string unido(const std::string &login) {
    for (auto &r : g_registrados) if (r.first == login) return r.second;
    return "";
}

namespace {
void tickAvatares() {
    for (auto it = g_avatares.begin(); it != g_avatares.end();) {
        int code = 0;
        if (!sys::finished(it->p, &code)) { ++it; continue; }
        // la foto se guarda con su extension de verdad (raylib elige el formato por la extension)
        std::string head = leer(it->dest).substr(0, 4), ext = head.rfind("\x89PNG", 0) == 0 ? ".png" : head.rfind("\xFF\xD8", 0) == 0 ? ".jpg" : "";
        std::error_code ec;
        if (code == 0 && !ext.empty()) {
            std::string fin = it->dest.substr(0, it->dest.size() - 4) + ext;
            fs::rename(fs::u8path(it->dest), fs::u8path(fin), ec);
            if (!ec) g_perfiles[it->login].avatar = fin;
        } else fs::remove(fs::u8path(it->dest), ec);
        it = g_avatares.erase(it);
    }
}
}  // namespace

void tick() {
    tickAvatares();
    if (g_activa && sys::finished(g_proc)) terminarPeticion();
    lanzarSiguiente();
    if (g_entrando && !g_activa && g_cola.empty() && reloj() >= g_proximoSondeo) {
        if (reloj() > g_caduca) { g_entrando = false; g_codigo.clear(); g_errCuenta = "El código caducó: vuelve a pulsar Entrar."; }
        else { g_proximoSondeo = reloj() + 1e9; sondearToken(); }
    }
    if (g_enviando && g_env.intentos > 0 && g_env.sha.empty() && !g_activa && g_cola.empty() && reloj() >= g_proximoSondeo) {
        g_proximoSondeo = reloj() + 1e9;
        esperarCopia();
    }
}

bool conectado() { return !g_token.empty() && !g_user.empty(); }
std::string usuario() { return g_user; }
void entrar() {
    if (g_entrando) return;
    g_errCuenta.clear();
    if (g_clientId.empty()) { g_errCuenta = "Falta registrar la aplicación de Source 2D en GitHub (pon su Client ID en src/editor/envio.cpp)."; return; }
    g_entrando = true; g_codigo.clear();
    pedir({"POST", g_web + "/login/device/code", "client_id=" + urlEnc(g_clientId) + "&scope=public_repo%20user%3Afollow", true, false, [](int code, const json &b) {
        if (code != 200 || !b.is_object() || !b.contains("user_code") || !b.contains("device_code")) {
            g_entrando = false;
            g_errCuenta = code == 0 ? "Sin conexión con GitHub." : "GitHub no dio un código (" + std::to_string(code) + ").";
            return;
        }
        g_codigo = b["user_code"].get<std::string>();
        g_deviceCode = b["device_code"].get<std::string>();
        g_urlCodigo = b.value("verification_uri", g_web + "/login/device");
        g_intervalo = std::max(1, b.value("interval", 5));
        g_caduca = reloj() + b.value("expires_in", 900);
        g_proximoSondeo = reloj() + g_intervalo;
    }});
}
bool entrando() { return g_entrando; }
std::string codigo() { return g_codigo; }
std::string urlCodigo() { return g_urlCodigo; }
void salir() {
    g_token.clear(); g_user.clear(); g_entrando = false; g_codigo.clear(); g_mios.clear();
    guardarCuenta();
}
std::string errorCuenta() { return g_errCuenta; }

void enviar(const Paquete &p) {
    if (g_enviando || !conectado()) return;
    g_env = Envio{};
    g_env.p = p;
    g_env.rama = "envio-" + p.id + "-" + std::to_string((long long)time(nullptr));
    g_enviando = true; g_fallo = false;
    g_estado = "Creando tu copia del repositorio de envíos...";
    pedir({"POST", g_api + "/repos/" + kDueno + "/" + kRepo + "/forks", json{{"default_branch_only", true}}.dump(), false, true, [](int code, const json &b) {
        if (code != 202 && code != 200) { fallar("GitHub no dejó copiar el repositorio de envíos (" + std::to_string(code) + (b.is_object() ? " " + b.value("message", "") : "") + ")."); return; }
        g_env.intentos = 1;
        esperarCopia();
    }});
}
bool enviando() { return g_enviando; }
std::string estadoEnvio() { return g_estado; }
bool envioFallo() { return g_fallo; }

const std::vector<Mio> &mios() { return g_mios; }
void mirarMios() {
    if (!conectado() || reloj() - g_misAt < (g_pruebas ? 1 : 60)) return;
    g_misAt = reloj();
    for (auto &m : g_mios) {
        if (m.estado != "revisando" || !m.numero) continue;
        int n = m.numero;
        // el revisor cierra el envio con un comentario que empieza por "Aprobado" o "Rechazado: motivo"
        pedir({"GET", g_api + "/repos/" + kDueno + "/" + kRepo + "/issues/" + std::to_string(n) + "/comments", "", false, true, [n](int code, const json &b) {
            if (code != 200 || !b.is_array()) return;
            for (auto it = b.rbegin(); it != b.rend(); ++it) {
                if (!it->is_object() || !(*it).contains("user") || (*it)["user"].value("login", "") != kDueno) continue;
                std::string txt = it->value("body", "");
                for (auto &m : g_mios) if (m.numero == n) {
                    if (txt.rfind("Aprobado", 0) == 0) m.estado = "aprobado";
                    else if (txt.rfind("Rechazado", 0) == 0) {
                        m.estado = "rechazado";
                        size_t c = txt.find(':');
                        m.motivo = c == std::string::npos ? "" : txt.substr(c + 1);
                        while (!m.motivo.empty() && m.motivo[0] == ' ') m.motivo.erase(0, 1);
                    } else continue;
                    guardarMio(m);
                }
                break;
            }
        }});
    }
}
}  // namespace envio
