#include "envio.hpp"
#include "servidor.hpp"
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
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace envio {
namespace {
const char *kClientId = "Ov23lipmhcQcBtNRYcEX";
const long long kParte = 95000000;

bool g_pruebas = false, g_apiOn = true;
std::string g_api = "https://api.github.com", g_web = "https://github.com", g_clientId = kClientId;
std::string g_token, g_user, g_errCuenta, g_codigo, g_urlCodigo, g_deviceCode, g_estado;
bool g_entrando = false, g_enviando = false, g_fallo = false, g_sondeando = false;
double g_intervalo = 5, g_proximoSondeo = 0, g_caduca = 0, g_yoAt = -1e9;
bool g_yoPedido = false;
std::vector<Mio> g_mios;
Yo g_yo;
std::map<std::string, std::string> g_acciones;
std::set<std::string> g_fallos;

std::string dir() { return ui::configDir(); }
double reloj() { return (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0; }
double cada(double normal) { return g_pruebas ? 1 : normal; }
std::string leer(const std::string &p) {
    std::ifstream in(fs::u8path(p), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
bool escribirPrivado(const std::string &p, const std::string &data) {
    std::error_code ec;
    fs::create_directories(fs::u8path(p).parent_path(), ec);
    { std::ofstream o(fs::u8path(p), std::ios::binary | std::ios::trunc); }
    fs::permissions(fs::u8path(p), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    std::ofstream o(fs::u8path(p), std::ios::binary | std::ios::trunc);
    return (bool)(o << data);
}
std::string urlEnc(const std::string &s) {
    std::string e;
    for (unsigned char c : s) {
        if (isalnum(c) || strchr("-_.~", c)) e += (char)c;
        else { char h[4]; snprintf(h, sizeof h, "%%%02X", c); e += h; }
    }
    return e;
}
std::string minus(std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; }
bool loginSeguro(const std::string &s) {
    if (s.empty() || s.size() > 39) return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || c == '-')) return false;
    return true;
}
bool claveSegura(const std::string &s) {
    if (s.empty() || s.size() > 80) return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || c == '-' || c == '_') || isupper((unsigned char)c)) return false;
    return true;
}
std::string str(const json &j, const char *k) { return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }
std::string error(int code, const json &b, const std::string &defecto) {
    std::string e = str(b, "error");
    if (!e.empty()) return e;
    if (code == 0) return "Sin conexión con el servidor de Source 2D.";
    return defecto + " (" + std::to_string(code) + ").";
}
void accion(const std::string &a, const std::string &msg, bool fallo) {
    g_acciones[a] = msg;
    if (fallo) g_fallos.insert(a); else g_fallos.erase(a);
}

enum class Tipo { GitHubForm, GitHub, Api, Publico };
struct Pet {
    std::string metodo, url, cuerpo;
    Tipo tipo = Tipo::GitHub;
    std::function<void(int, const json &)> fin;
    std::string archivo, salida;
    bool conToken = true;
};
std::deque<Pet> g_cola;
bool g_activa = false;
sys::Proc g_proc;
Pet g_actual;

std::string tmp(const char *nm) { return dir() + "/servidor-" + nm; }
void lanzarSiguiente() {
    if (g_activa || g_cola.empty()) return;
    g_actual = std::move(g_cola.front());
    g_cola.pop_front();
    std::string hdr = "User-Agent: Source2D\n";
    if (g_actual.tipo == Tipo::GitHubForm) hdr += "Accept: application/json\n";
    else if (g_actual.tipo == Tipo::GitHub) hdr += "Accept: application/vnd.github+json\nX-GitHub-Api-Version: 2022-11-28\nContent-Type: application/json\n";
    else if (g_actual.tipo == Tipo::Api) hdr += std::string("Accept: application/json\nContent-Type: ") + (g_actual.archivo.empty() ? "application/json\n" : "application/octet-stream\n");
    if (g_actual.conToken && !g_token.empty() && (g_actual.tipo == Tipo::GitHub || g_actual.tipo == Tipo::Api)) hdr += "Authorization: Bearer " + g_token + "\n";
    escribirPrivado(tmp("cabeceras"), hdr);
    std::error_code ec;
    std::string salida = g_actual.salida.empty() ? tmp("respuesta") : g_actual.salida;
    fs::remove(fs::u8path(salida), ec);
    fs::remove(fs::u8path(tmp("estado")), ec);
    std::vector<std::string> a = {"curl", "-sL", "--proto", g_pruebas ? "=https,http,file" : "=https", "--max-time", g_actual.archivo.empty() ? "120" : "3600",
                                  "-H", "@" + tmp("cabeceras"), "-D", tmp("estado"), "-o", salida};
    if (!g_actual.archivo.empty()) a.insert(a.end(), {"-X", g_actual.metodo, "-T", g_actual.archivo});
    else {
        a.insert(a.end(), {"-X", g_actual.metodo});
        if (!g_actual.cuerpo.empty()) {
            escribirPrivado(tmp("cuerpo"), g_actual.cuerpo);
            a.insert(a.end(), {"--data-binary", "@" + tmp("cuerpo")});
        }
    }
    a.push_back(g_actual.url);
    g_activa = sys::spawn(a, &g_proc, true);
    if (!g_activa) { auto f = g_actual.fin; if (f) f(0, json()); }
}
void pedir(Pet p) { g_cola.push_back(std::move(p)); lanzarSiguiente(); }
void pedirApi(const std::string &metodo, const std::string &ruta, const json &cuerpo, std::function<void(int, const json &)> fin) {
    if (!g_apiOn) { if (fin) fin(0, json()); return; }
    Pet p;
    p.metodo = metodo; p.url = servidor::api() + ruta; p.tipo = Tipo::Api; p.fin = std::move(fin);
    if (!cuerpo.is_null()) p.cuerpo = cuerpo.dump();
    pedir(std::move(p));
}
void pedirPublico(const std::string &ruta, std::function<void(int, const json &)> fin) {
    Pet p;
    p.metodo = "GET"; p.url = servidor::r2() + ruta;
    if (p.url.rfind("https://", 0) == 0) p.url += "?t=" + std::to_string((long long)time(nullptr));
    p.tipo = Tipo::Publico; p.conToken = false; p.fin = std::move(fin);
    pedir(std::move(p));
}
void terminarPeticion() {
    g_activa = false;
    std::string st = leer(tmp("estado"));
    int code = 0;
    for (size_t p = 0; p < st.size(); p = st.find('\n', p) == std::string::npos ? st.size() : st.find('\n', p) + 1) {
        if (st.compare(p, 5, "HTTP/") != 0) continue;
        size_t sp = st.find(' ', p);
        if (sp != std::string::npos) code = atoi(st.c_str() + sp + 1);
    }
    if (code == 0 && g_actual.url.rfind("file://", 0) == 0) {
        std::error_code ec;
        code = fs::exists(fs::u8path(g_actual.salida.empty() ? tmp("respuesta") : g_actual.salida), ec) ? 200 : 404;
    }
    json body = g_actual.salida.empty() ? json::parse(leer(tmp("respuesta")), nullptr, false) : json();
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
Mio mioDe(const json &x) {
    Mio m;
    m.id = str(x, "id"); m.tipo = str(x, "tipo"); m.nombre = str(x, "nombre"); m.fecha = str(x, "fecha").substr(0, 10);
    m.estado = str(x, "estado"); m.motivo = str(x, "motivo"); m.url = str(x, "enlace");
    if (m.estado == "pendiente" || m.estado == "subiendo") m.estado = "revisando";
    return m;
}
void ponerMios(const json &lista) {
    if (!lista.is_array()) return;
    std::vector<Mio> r;
    for (auto &x : lista) if (x.is_object()) r.push_back(mioDe(x));
    std::sort(r.begin(), r.end(), [](const Mio &a, const Mio &b) { return a.fecha == b.fecha ? a.id < b.id : a.fecha < b.fecha; });
    g_mios.swap(r);
}
void pedirYo() {
    if (!g_apiOn || g_token.empty() || g_yoPedido) return;
    g_yoPedido = true;
    g_yoAt = reloj();
    pedirApi("GET", "/yo", json(), [](int code, const json &b) {
        g_yoPedido = false;
        if (code != 200 || !b.is_object()) return;
        Yo y;
        y.listo = true;
        y.registrado = b.value("registrado", false); y.beta = b.value("beta", false); y.unido = str(b, "unido").substr(0, 10);
        if (b.contains("baneo") && b["baneo"].is_object()) { y.baneado = true; y.baneoHasta = str(b["baneo"], "hasta"); y.baneoMotivo = str(b["baneo"], "motivo"); }
        if (b.contains("permisos") && b["permisos"].is_object()) { y.grandeMb = b["permisos"].value("grande_mb", 0); y.nubeMb = b["permisos"].value("nube_mb", 0); }
        y.sinLeer = b.value("sinLeer", 0);
        g_yo = y;
        if (b.contains("envios")) ponerMios(b["envios"]);
        if (!y.registrado && !y.baneado) pedirApi("POST", "/registro", json::object(), nullptr);
    });
}
void pedirUsuario() {
    Pet p{"GET", g_api + "/user", "", Tipo::GitHub, [](int code, const json &b) {
        if (code == 200 && b.is_object() && b.contains("login")) {
            g_user = b["login"].get<std::string>(); g_errCuenta.clear(); guardarCuenta();
            pedirApi("POST", "/registro", json::object(), [](int, const json &) { g_yoAt = -1e9; });
        } else if (code == 401) { g_token.clear(); g_user.clear(); guardarCuenta(); g_errCuenta = "GitHub ya no acepta la sesión: vuelve a entrar."; }
        else if (code == 0) g_errCuenta = "Sin conexión con GitHub.";
    }};
    pedir(std::move(p));
}
void sondearToken() {
    std::string cuerpo = "client_id=" + urlEnc(g_clientId) + "&device_code=" + urlEnc(g_deviceCode) +
                         "&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code";
    g_sondeando = true;
    Pet p{"POST", g_web + "/login/oauth/access_token", cuerpo, Tipo::GitHubForm, [](int code, const json &b) {
        g_sondeando = false;
        if (!g_entrando) return;
        std::string err = str(b, "error");
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
    }};
    p.conToken = false;
    pedir(std::move(p));
}

struct Subida {
    std::string ruta, archivo;
    long long tam = 0;
};
struct Envio {
    Paquete p;
    std::string id, tipo;
    std::vector<std::string> pendientes;
    std::vector<std::string> temporales;
    std::string subida;
    int parte = 0, partes = 0;
    json etags = json::array();
    std::string nombreActual;
};
Envio g_env;
void limpiarTemporales() {
    std::error_code ec;
    for (auto &t : g_env.temporales) fs::remove(fs::u8path(t), ec);
    g_env.temporales.clear();
}
void fallar(const std::string &msg) { limpiarTemporales(); g_enviando = false; g_fallo = true; g_estado = msg; }
void cerrarEnvio();
void subirSiguiente();
void subirParte() {
    if (g_env.parte >= g_env.partes) {
        pedirApi("POST", "/partes/" + g_env.subida + "/fin", json{{"partes", g_env.etags}}, [](int code, const json &b) {
            if (code != 200) { fallar(error(code, b, "No se pudo terminar de subir " + g_env.nombreActual)); return; }
            subirSiguiente();
        });
        return;
    }
    const std::string &datos = g_env.p.archivos[g_env.nombreActual];
    long long desde = (long long)g_env.parte * kParte;
    std::string trozo = datos.substr((size_t)desde, (size_t)std::min<long long>(kParte, (long long)datos.size() - desde));
    std::string t = tmp(("parte-" + std::to_string(g_env.parte + 1)).c_str());
    escribirPrivado(t, trozo);
    g_env.temporales.push_back(t);
    g_estado = "Subiendo " + g_env.nombreActual + " (" + std::to_string(g_env.parte + 1) + " de " + std::to_string(g_env.partes) + ")...";
    Pet p;
    p.metodo = "PUT"; p.url = servidor::api() + "/partes/" + g_env.subida + "/" + std::to_string(g_env.parte + 1); p.tipo = Tipo::Api; p.archivo = t;
    p.fin = [t](int code, const json &b) {
        std::error_code ec;
        fs::remove(fs::u8path(t), ec);
        if (code != 200) { fallar(error(code, b, "No se pudo subir " + g_env.nombreActual)); return; }
        g_env.etags.push_back({{"n", b.value("n", g_env.parte + 1)}, {"etag", str(b, "etag")}});
        g_env.parte++;
        subirParte();
    };
    pedir(std::move(p));
}
void subirSiguiente() {
    if (g_env.pendientes.empty()) { cerrarEnvio(); return; }
    std::string nm = g_env.pendientes.back();
    g_env.pendientes.pop_back();
    g_env.nombreActual = nm;
    const std::string &datos = g_env.p.archivos[nm];
    if ((long long)datos.size() > kParte) {
        g_estado = "Preparando la subida de " + nm + "...";
        g_env.parte = 0; g_env.partes = (int)(((long long)datos.size() + kParte - 1) / kParte); g_env.etags = json::array();
        pedirApi("POST", "/partes", json{{"destino", "envio"}, {"id", g_env.id}, {"archivo", nm}, {"tam", (long long)datos.size()}}, [](int code, const json &b) {
            if (code != 201) { fallar(error(code, b, "No se pudo subir " + g_env.nombreActual)); return; }
            g_env.subida = str(b, "subida");
            subirParte();
        });
        return;
    }
    std::string t = tmp(("archivo-" + nm).c_str());
    escribirPrivado(t, datos);
    g_env.temporales.push_back(t);
    g_estado = "Subiendo " + nm + "...";
    Pet p;
    p.metodo = "PUT"; p.url = servidor::api() + "/envios/" + g_env.id + "/" + nm; p.tipo = Tipo::Api; p.archivo = t;
    p.fin = [nm, t](int code, const json &b) {
        std::error_code ec;
        fs::remove(fs::u8path(t), ec);
        if (code != 200) { fallar(error(code, b, "No se pudo subir " + nm)); return; }
        subirSiguiente();
    };
    pedir(std::move(p));
}
void cerrarEnvio() {
    g_estado = "Enviando a revisión...";
    pedirApi("POST", "/envios/" + g_env.id + "/listo", json::object(), [](int code, const json &b) {
        if (code != 200) { fallar(error(code, b, "No se pudo enviar a revisión")); return; }
        limpiarTemporales();
        g_mios.push_back(mioDe(b));
        g_enviando = false; g_fallo = false;
        g_estado = "¡Enviado! Ahora lo revisa el equipo de Source 2D. Aquí verás si se aprueba.";
        g_env = Envio{};
        g_yoAt = -1e9;
    });
}
}

void iniciar(bool pruebas) {
    g_pruebas = pruebas;
    if (const char *a = getenv("S2D_GITHUB_API")) g_api = a;
    if (const char *w = getenv("S2D_GITHUB_WEB")) g_web = w;
    if (const char *c = getenv("S2D_GITHUB_CLIENT_ID")) g_clientId = c;
    g_apiOn = !pruebas || getenv("S2D_API_URL");
    if (pruebas && !getenv("S2D_GITHUB_API")) return;
    json j = json::parse(leer(dir() + "/github.json"), nullptr, false);
    if (j.is_object()) { g_token = j.value("token", ""); g_user = j.value("usuario", ""); }
    if (!g_token.empty()) pedirUsuario();
}

namespace {
std::map<std::string, Perfil> g_perfiles;
std::map<std::string, double> g_perfilAt;
std::vector<std::pair<std::string, std::string>> g_registrados, g_regNuevos;
std::set<std::string> g_regSet, g_betas, g_betasNuevos;
bool g_registradosListos = false, g_regPidiendo = false;
double g_registradosAt = -1e9;
std::set<std::string> g_yoSigo;
bool g_yoSigoListo = false, g_yoSigoPedido = false;
struct Avatar { sys::Proc p; std::string login, dest; };
std::vector<Avatar> g_avatares;
std::vector<std::string> logins(const json &b) {
    std::vector<std::string> r;
    if (b.is_array()) for (auto &x : b) if (x.is_object() && x.contains("login") && x["login"].is_string()) r.push_back(x["login"]);
    return r;
}
void paginas(const std::string &url, int pag, std::shared_ptr<std::vector<std::string>> acc, std::function<void(std::vector<std::string> &)> fin) {
    pedir({"GET", url + (url.find('?') == std::string::npos ? "?" : "&") + "per_page=100&page=" + std::to_string(pag), "", Tipo::GitHub,
           [url, pag, acc, fin](int code, const json &b) {
               auto l = code == 200 ? logins(b) : std::vector<std::string>();
               acc->insert(acc->end(), l.begin(), l.end());
               if (l.size() == 100 && pag < 10) paginas(url, pag + 1, acc, fin);
               else fin(*acc);
           }});
}
void pedirYoSigo() {
    if (!conectado() || g_yoSigoPedido) return;
    g_yoSigoPedido = true;
    paginas(g_api + "/user/following", 1, std::make_shared<std::vector<std::string>>(), [](std::vector<std::string> &l) {
        g_yoSigo.clear();
        for (auto &x : l) g_yoSigo.insert(minus(x));
        g_yoSigoListo = true;
    });
}
void pedirRegistradosPag(const std::string &cursor, int vuelta) {
    pedirApi("GET", "/registrados" + (cursor.empty() ? "" : "?cursor=" + urlEnc(cursor)), json(), [vuelta](int code, const json &b) {
        if (code != 200 || !b.is_object() || !b.contains("cuentas") || !b["cuentas"].is_array()) { g_regPidiendo = false; g_registradosAt = -1e9; return; }
        for (auto &x : b["cuentas"]) {
            std::string l = str(x, "login");
            if (!loginSeguro(l)) continue;
            g_regNuevos.push_back({l, str(x, "unido").substr(0, 10)});
            if (x.value("beta", false)) g_betasNuevos.insert(minus(l));
        }
        std::string c = str(b, "cursor");
        if (!c.empty() && vuelta < 50) { pedirRegistradosPag(c, vuelta + 1); return; }
        std::sort(g_regNuevos.begin(), g_regNuevos.end(), [](auto &a, auto &b) { return a.second > b.second; });
        g_registrados.swap(g_regNuevos);
        g_betas.swap(g_betasNuevos);
        g_regNuevos.clear(); g_betasNuevos.clear();
        g_regSet.clear();
        for (auto &r : g_registrados) g_regSet.insert(minus(r.first));
        g_registradosListos = true;
        g_regPidiendo = false;
    });
}
void leerPerfilSP(const std::string &login) {
    pedirPublico("perfiles/" + minus(login) + ".json", [login](int code, const json &b) {
        Perfil &p = g_perfiles[login];
        p.spCargado = true;
        if (code != 200 || !b.is_object()) return;
        p.frase = str(b, "frase"); p.color = str(b, "color"); p.empezar = str(b, "empezar"); p.insignia = str(b, "insignia");
        p.clavePublica = str(b, "clavePublica");
        p.destacados.clear();
        if (b.contains("destacados") && b["destacados"].is_object())
            for (auto &[k, v] : b["destacados"].items()) if (v.is_string()) p.destacados[k] = v.get<std::string>();
    });
}
}

void pedirPerfil(const std::string &login) {
    pedirRegistrados();
    pedirYoSigo();
    if (!loginSeguro(login)) return;
    if (g_perfiles.count(login)) {
        if (reloj() - g_perfilAt[login] > cada(45)) refrescarPerfil(login);
        return;
    }
    Perfil &p = g_perfiles[login];
    p.login = login;
    g_perfilAt[login] = reloj();
    leerPerfilSP(login);
    pedir({"GET", g_api + "/users/" + login, "", Tipo::GitHub, [login](int code, const json &b) {
        Perfil &p = g_perfiles[login];
        if (code != 200 || !b.is_object()) { p.error = true; return; }
        p.cargado = true;
        p.nombre = str(b, "name");
        p.bio = str(b, "bio");
        p.creado = str(b, "created_at").substr(0, 10);
        std::string av = str(b, "avatar_url");
        if (av.rfind("https://", 0) == 0 || (g_pruebas && av.rfind("file://", 0) == 0)) {
            Avatar a; a.login = login; a.dest = dir() + "/avatares/" + login + ".img";
            std::error_code ec;
            fs::create_directories(fs::u8path(a.dest).parent_path(), ec);
            std::string url = av + (av.find('?') == std::string::npos ? "?s=160" : "&s=160");
            if (sys::spawn({"curl", "-fsL", "--proto", g_pruebas ? "=https,file" : "=https", "--max-time", "30", "--max-filesize", "2000000", "-o", a.dest, url}, &a.p, true))
                g_avatares.push_back(a);
        }
    }});
    pedirListas(login);
}
void refrescarPerfil(const std::string &login) {
    if (!loginSeguro(login)) return;
    g_perfilAt[login] = reloj();
    leerPerfilSP(login);
    auto it = g_perfiles.find(login);
    if (it != g_perfiles.end() && it->second.seguidoresListos && it->second.siguiendoListos) {
        it->second.listas = false;
        pedirListas(login);
    }
}
const Perfil *perfil(const std::string &login) {
    auto it = g_perfiles.find(login);
    if (it == g_perfiles.end()) return nullptr;
    Perfil &p = it->second;
    p.seguidoresSP.clear(); p.siguiendoSP.clear();
    for (auto &l : p.listaSeguidores) if (g_regSet.count(minus(l))) p.seguidoresSP.push_back(l);
    for (auto &l : p.listaSiguiendo) if (g_regSet.count(minus(l))) p.siguiendoSP.push_back(l);
    bool listo = g_registradosListos && p.seguidoresListos && p.siguiendoListos;
    p.seguidores = listo ? (int)p.seguidoresSP.size() : -1;
    p.siguiendo = listo ? (int)p.siguiendoSP.size() : -1;
    p.sigo = !conectado() || login == g_user || !g_yoSigoListo ? -1 : g_yoSigo.count(minus(login)) ? 1 : 0;
    return &p;
}
void pedirListas(const std::string &login) {
    auto it = g_perfiles.find(login);
    if (it == g_perfiles.end() || it->second.listas) return;
    it->second.listas = true;
    paginas(g_api + "/users/" + login + "/followers", 1, std::make_shared<std::vector<std::string>>(), [login](std::vector<std::string> &l) {
        g_perfiles[login].listaSeguidores = l; g_perfiles[login].seguidoresListos = true;
    });
    paginas(g_api + "/users/" + login + "/following", 1, std::make_shared<std::vector<std::string>>(), [login](std::vector<std::string> &l) {
        g_perfiles[login].listaSiguiendo = l; g_perfiles[login].siguiendoListos = true;
    });
}
int sigo(const std::string &login) {
    if (!conectado() || minus(login) == minus(g_user)) return -1;
    pedirYoSigo();
    return g_yoSigoListo ? (g_yoSigo.count(minus(login)) ? 1 : 0) : -1;
}
void seguir(const std::string &login, bool si) {
    if (!conectado() || !loginSeguro(login) || login == g_user) return;
    pedir({si ? "PUT" : "DELETE", g_api + "/user/following/" + login, "", Tipo::GitHub, [login, si](int code, const json &) {
        if (code != 204) return;
        if (si) g_yoSigo.insert(minus(login)); else g_yoSigo.erase(minus(login));
        auto quita = [](std::vector<std::string> &v, const std::string &x) { v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string &y) { return minus(y) == minus(x); }), v.end()); };
        auto it = g_perfiles.find(login);
        if (it != g_perfiles.end()) { quita(it->second.listaSeguidores, g_user); if (si) it->second.listaSeguidores.push_back(g_user); }
        auto yo = g_perfiles.find(g_user);
        if (yo != g_perfiles.end()) { quita(yo->second.listaSiguiendo, login); if (si) yo->second.listaSiguiendo.push_back(login); }
    }});
}
void pedirRegistrados() {
    if (g_regPidiendo || reloj() - g_registradosAt < cada(120)) return;
    g_registradosAt = reloj();
    g_regPidiendo = true;
    g_regNuevos.clear(); g_betasNuevos.clear();
    pedirRegistradosPag("", 0);
}
const std::vector<std::pair<std::string, std::string>> &registrados() { return g_registrados; }
std::string unido(const std::string &login) {
    for (auto &r : g_registrados) if (minus(r.first) == minus(login)) return r.second;
    return "";
}
bool esBeta(const std::string &login) { return g_betas.count(minus(login)) > 0; }
void guardarPerfil(const json &campos) {
    if (!conectado()) { accion("perfil", "Entra con GitHub para configurar tu perfil.", true); return; }
    accion("perfil", "Guardando...", false);
    std::string yo = g_user;
    pedirApi("PUT", "/perfil", campos, [yo](int code, const json &b) {
        if (code != 200 || !b.is_object()) { accion("perfil", error(code, b, "No se pudo guardar el perfil"), true); return; }
        Perfil &p = g_perfiles[yo];
        p.spCargado = true;
        p.frase = str(b, "frase"); p.color = str(b, "color"); p.empezar = str(b, "empezar"); p.insignia = str(b, "insignia"); p.clavePublica = str(b, "clavePublica");
        p.destacados.clear();
        if (b.contains("destacados") && b["destacados"].is_object())
            for (auto &[k, v] : b["destacados"].items()) if (v.is_string()) p.destacados[k] = v.get<std::string>();
        accion("perfil", "Perfil guardado.", false);
    });
}

namespace {
void tickAvatares() {
    for (auto it = g_avatares.begin(); it != g_avatares.end();) {
        int code = 0;
        if (!sys::finished(it->p, &code)) { ++it; continue; }
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
}

void tick() {
    tickAvatares();
    if (g_activa && sys::finished(g_proc)) terminarPeticion();
    lanzarSiguiente();
    if (g_entrando && !g_sondeando && !g_deviceCode.empty() && reloj() >= g_proximoSondeo) {
        if (reloj() > g_caduca) { g_entrando = false; g_codigo.clear(); g_errCuenta = "El código caducó: vuelve a pulsar Entrar."; }
        else sondearToken();
    }
    if (conectado() && reloj() - g_yoAt > cada(60)) pedirYo();
}

bool conectado() { return !g_token.empty() && !g_user.empty(); }
std::string usuario() { return g_user; }
void entrar() {
    if (g_entrando) return;
    g_errCuenta.clear();
    if (g_clientId.empty()) { g_errCuenta = "Falta registrar la aplicación de Source 2D en GitHub (pon su Client ID en src/editor/envio.cpp)."; return; }
    g_entrando = true; g_codigo.clear(); g_deviceCode.clear(); g_sondeando = false;
    Pet p{"POST", g_web + "/login/device/code", "client_id=" + urlEnc(g_clientId) + "&scope=user%3Afollow", Tipo::GitHubForm, [](int code, const json &b) {
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
    }};
    p.conToken = false;
    pedir(std::move(p));
}
bool entrando() { return g_entrando; }
std::string codigo() { return g_codigo; }
std::string urlCodigo() { return g_urlCodigo; }
void salir() {
    g_token.clear(); g_user.clear(); g_entrando = false; g_codigo.clear(); g_mios.clear();
    g_yoSigo.clear(); g_yoSigoListo = false; g_yoSigoPedido = false;
    g_yo = Yo{};
    guardarCuenta();
}
std::string errorCuenta() { return g_errCuenta; }

void enviar(const Paquete &p) {
    if (g_enviando || !conectado()) return;
    if (g_yo.baneado) { g_fallo = true; g_estado = "Tu cuenta está baneada: no puedes enviar."; return; }
    g_env = Envio{};
    g_env.p = p;
    g_env.tipo = p.carpeta == "sprites" || p.carpeta == "sonidos" ? p.carpeta : "juegos";
    long long tam = 0;
    for (auto &kv : p.archivos) tam += (long long)kv.second.size();
    g_enviando = true; g_fallo = false;
    g_estado = "Preparando el envío...";
    pedirApi("POST", "/envios", json{{"tipo", g_env.tipo}, {"nombre", p.nombre}, {"tam", tam}}, [](int code, const json &b) {
        if (code != 201 || !b.is_object()) { fallar(error(code, b, "No se pudo empezar el envío")); return; }
        g_env.id = str(b, "id");
        g_env.pendientes.clear();
        for (auto &kv : g_env.p.archivos) if (kv.first != "info.json") g_env.pendientes.push_back(kv.first);
        g_env.pendientes.push_back("info.json");
        std::reverse(g_env.pendientes.begin(), g_env.pendientes.end());
        subirSiguiente();
    });
}
bool enviando() { return g_enviando; }
std::string estadoEnvio() { return g_estado; }
bool envioFallo() { return g_fallo; }

namespace { std::string g_denuncia; bool g_denunciaFallo = false; }
void denunciar(const Denuncia &d) {
    if (!conectado()) { g_denuncia = "Entra con GitHub para denunciar."; g_denunciaFallo = true; return; }
    g_denuncia = "Enviando la denuncia..."; g_denunciaFallo = false;
    json c = {{"tipo", d.tipo}, {"objetivo", d.objetivo}, {"titulo", d.titulo}, {"cuerpo", d.cuerpo}, {"conversacion", d.conversacion}, {"mensajes", d.mensajes}};
    pedirApi("POST", "/denuncias", c, [](int code, const json &b) {
        if (code == 201) { g_denuncia = "Denuncia enviada. Gracias: la revisará el equipo de Source 2D."; g_denunciaFallo = false; }
        else { g_denuncia = error(code, b, "No se pudo enviar la denuncia"); g_denunciaFallo = true; }
    });
}
void denunciar(const std::string &titulo, const std::string &cuerpo) {
    Denuncia d;
    d.tipo = titulo.find("cuenta") != std::string::npos ? "cuenta" : titulo.find("paquete") != std::string::npos ? "paquete" : "juego";
    d.titulo = titulo; d.cuerpo = cuerpo;
    denunciar(d);
}
std::string estadoDenuncia() { return g_denuncia; }
bool denunciaFallo() { return g_denunciaFallo; }
void limpiarDenuncia() { if (g_denuncia != "Enviando la denuncia...") { g_denuncia.clear(); g_denunciaFallo = false; } }

const std::vector<Mio> &mios() { return g_mios; }
void mirarMios() {
    if (conectado() && reloj() - g_yoAt > cada(15)) pedirYo();
}
const Yo &yo() { return g_yo; }

namespace {
std::vector<Notificacion> g_notis;
bool g_notisListas = false, g_notisPidiendo = false;
double g_notisAt = -1e9;
}
void pedirNotificaciones() {
    if (!conectado() || g_notisPidiendo || reloj() - g_notisAt < cada(20)) return;
    g_notisPidiendo = true;
    g_notisAt = reloj();
    pedirApi("GET", "/notificaciones", json(), [](int code, const json &b) {
        g_notisPidiendo = false;
        if (code != 200 || !b.is_object() || !b.contains("notificaciones") || !b["notificaciones"].is_array()) return;
        std::string hasta = str(b, "leidasHasta");
        std::vector<Notificacion> r;
        for (auto &x : b["notificaciones"]) {
            if (!x.is_object()) continue;
            Notificacion n;
            n.id = str(x, "id"); n.tipo = str(x, "tipo"); n.titulo = str(x, "titulo"); n.texto = str(x, "texto"); n.enlace = str(x, "enlace"); n.fecha = str(x, "fecha");
            n.leida = !hasta.empty() && n.fecha <= hasta;
            r.push_back(n);
        }
        g_notis.swap(r);
        g_notisListas = true;
    });
}
const std::vector<Notificacion> &notificaciones() { return g_notis; }
bool notificacionesListas() { return g_notisListas; }
void marcarLeidas() {
    if (!conectado()) return;
    for (auto &n : g_notis) n.leida = true;
    g_yo.sinLeer = 0;
    pedirApi("POST", "/notificaciones/leidas", json::object(), nullptr);
}

namespace {
struct Hilo { std::vector<Comentario> lista; bool cargado = false, pidiendo = false; double at = -1e9; };
std::map<std::string, Hilo> g_hilos;
bool tipoSeguro(const std::string &t) { return t == "juegos" || t == "sprites" || t == "sonidos"; }
}
void pedirComentarios(const std::string &tipo, const std::string &id) {
    if (!tipoSeguro(tipo) || !claveSegura(id)) return;
    Hilo &h = g_hilos[tipo + "/" + id];
    if (h.pidiendo || reloj() - h.at < cada(30)) return;
    h.pidiendo = true;
    h.at = reloj();
    std::string k = tipo + "/" + id;
    pedirPublico("comentarios/" + k + ".json", [k](int code, const json &b) {
        Hilo &h = g_hilos[k];
        h.pidiendo = false;
        h.cargado = true;
        if (code != 200 || !b.is_object() || !b.contains("comentarios") || !b["comentarios"].is_array()) { if (code == 404) h.lista.clear(); return; }
        std::vector<Comentario> r;
        for (auto &x : b["comentarios"]) {
            if (!x.is_object() || !loginSeguro(str(x, "autor"))) continue;
            r.push_back({str(x, "id"), str(x, "autor"), str(x, "texto"), str(x, "fecha")});
        }
        h.lista.swap(r);
    });
}
const std::vector<Comentario> *comentarios(const std::string &tipo, const std::string &id) {
    auto it = g_hilos.find(tipo + "/" + id);
    return it == g_hilos.end() || !it->second.cargado ? nullptr : &it->second.lista;
}
void comentar(const std::string &tipo, const std::string &id, const std::string &texto) {
    if (!conectado()) { accion("comentario", "Entra con GitHub para comentar.", true); return; }
    if (!tipoSeguro(tipo) || !claveSegura(id)) return;
    accion("comentario", "Publicando...", false);
    std::string k = tipo + "/" + id;
    pedirApi("POST", "/comentarios/" + k, json{{"texto", texto}}, [k](int code, const json &b) {
        if (code != 201) { accion("comentario", error(code, b, "No se pudo publicar el comentario"), true); return; }
        Hilo &h = g_hilos[k];
        h.cargado = true;
        h.lista.push_back({str(b, "id"), str(b, "autor"), str(b, "texto"), str(b, "fecha")});
        accion("comentario", "", false);
    });
}
void darFeedback(const std::string &tipo, const std::string &id, const std::string &texto) {
    if (!conectado()) { accion("feedback", "Entra con GitHub para mandar feedback.", true); return; }
    if (!tipoSeguro(tipo) || !claveSegura(id)) return;
    accion("feedback", "Enviando...", false);
    pedirApi("POST", "/feedback/" + tipo + "/" + id, json{{"texto", texto}}, [](int code, const json &b) {
        if (code != 201) accion("feedback", error(code, b, "No se pudo mandar el feedback"), true);
        else accion("feedback", "¡Gracias! El creador recibirá tu feedback.", false);
    });
}

namespace {
std::vector<Feedback> g_feedback;
double g_feedbackAt = -1e9;
std::vector<Solicitud> g_solicitudes;
double g_solicitudesAt = -1e9;
}
void pedirFeedback() {
    if (!conectado() || reloj() - g_feedbackAt < cada(30)) return;
    g_feedbackAt = reloj();
    pedirApi("GET", "/feedback", json(), [](int code, const json &b) {
        if (code != 200 || !b.is_object() || !b.contains("feedback") || !b["feedback"].is_array()) return;
        std::vector<Feedback> r;
        for (auto &x : b["feedback"])
            if (x.is_object()) r.push_back({str(x, "id"), str(x, "de"), str(x, "tipo"), str(x, "juego"), str(x, "nombre"), str(x, "texto"), str(x, "fecha")});
        g_feedback.swap(r);
    });
}
const std::vector<Feedback> &feedbackRecibido() { return g_feedback; }

void solicitar(const std::string &tipo, int mb, const std::string &motivo, const std::string &proyecto) {
    if (!conectado()) { accion("solicitud", "Entra con GitHub para pedir permiso.", true); return; }
    accion("solicitud", "Enviando la solicitud...", false);
    pedirApi("POST", "/solicitudes", json{{"tipo", tipo}, {"mb", mb}, {"motivo", motivo}, {"proyecto", proyecto}}, [](int code, const json &b) {
        if (code != 201) { accion("solicitud", error(code, b, "No se pudo enviar la solicitud"), true); return; }
        accion("solicitud", "Solicitud enviada. Te llegará una notificación con la respuesta.", false);
        g_solicitudesAt = -1e9;
        pedirSolicitudes();
    });
}
void pedirSolicitudes() {
    if (!conectado() || reloj() - g_solicitudesAt < cada(30)) return;
    g_solicitudesAt = reloj();
    pedirApi("GET", "/solicitudes", json(), [](int code, const json &b) {
        if (code != 200 || !b.is_object() || !b.contains("solicitudes") || !b["solicitudes"].is_array()) return;
        std::vector<Solicitud> r;
        for (auto &x : b["solicitudes"]) {
            if (!x.is_object()) continue;
            Solicitud s;
            s.id = str(x, "id"); s.tipo = str(x, "tipo"); s.motivo = str(x, "motivo"); s.proyecto = str(x, "proyecto");
            s.estado = str(x, "estado"); s.respuesta = str(x, "respuesta"); s.fecha = str(x, "fecha").substr(0, 10); s.mb = x.value("mb", 0);
            r.push_back(s);
        }
        g_solicitudes.swap(r);
    });
}
const std::vector<Solicitud> &solicitudes() { return g_solicitudes; }

namespace {
std::vector<ProyectoNube> g_nube;
long long g_nubeCupo = 0, g_nubeUsado = 0;
bool g_nubeLista = false, g_nubeOcupada = false;
double g_nubeAt = -1e9;
struct SubidaNube { std::string nombre, datos, subida; int parte = 0, partes = 0; json etags = json::array(); };
SubidaNube g_subNube;
void nubeFallo(int code, const json &b, const std::string &defecto) {
    g_nubeOcupada = false;
    accion("nube", error(code, b, defecto), true);
    g_subNube = SubidaNube{};
}
void nubeHecha(const std::string &msg) {
    g_nubeOcupada = false;
    accion("nube", msg, false);
    g_subNube = SubidaNube{};
    g_nubeAt = -1e9;
    pedirNube();
}
void subirParteNube() {
    SubidaNube &s = g_subNube;
    if (s.parte >= s.partes) {
        pedirApi("POST", "/partes/" + s.subida + "/fin", json{{"partes", s.etags}}, [](int code, const json &b) {
            if (code != 200) nubeFallo(code, b, "No se pudo terminar de subir el proyecto");
            else nubeHecha("Proyecto guardado en la nube.");
        });
        return;
    }
    long long desde = (long long)s.parte * kParte;
    std::string t = tmp("nube-parte");
    escribirPrivado(t, s.datos.substr((size_t)desde, (size_t)std::min<long long>(kParte, (long long)s.datos.size() - desde)));
    accion("nube", "Subiendo a la nube (" + std::to_string(s.parte + 1) + " de " + std::to_string(s.partes) + ")...", false);
    Pet p;
    p.metodo = "PUT"; p.url = servidor::api() + "/partes/" + s.subida + "/" + std::to_string(s.parte + 1); p.tipo = Tipo::Api; p.archivo = t;
    p.fin = [t](int code, const json &b) {
        std::error_code ec;
        fs::remove(fs::u8path(t), ec);
        if (code != 200) { nubeFallo(code, b, "No se pudo subir el proyecto"); return; }
        g_subNube.etags.push_back({{"n", b.value("n", g_subNube.parte + 1)}, {"etag", str(b, "etag")}});
        g_subNube.parte++;
        subirParteNube();
    };
    pedir(std::move(p));
}
}
void pedirNube() {
    if (!conectado() || reloj() - g_nubeAt < cada(30)) return;
    g_nubeAt = reloj();
    pedirApi("GET", "/nube", json(), [](int code, const json &b) {
        if (code != 200 || !b.is_object()) return;
        g_nubeCupo = b.value("cupo", 0LL); g_nubeUsado = b.value("usado", 0LL);
        std::vector<ProyectoNube> r;
        if (b.contains("proyectos") && b["proyectos"].is_array())
            for (auto &x : b["proyectos"]) if (x.is_object()) r.push_back({str(x, "nombre"), str(x, "fecha").substr(0, 10), x.value("tam", 0LL)});
        g_nube.swap(r);
        g_nubeLista = true;
    });
}
bool nubeLista() { return g_nubeLista; }
long long nubeCupo() { return g_nubeCupo; }
long long nubeUsado() { return g_nubeUsado; }
const std::vector<ProyectoNube> &nube() { return g_nube; }
bool nubeOcupada() { return g_nubeOcupada; }
void subirNube(const std::string &nombre, const std::string &datos) {
    if (!conectado() || g_nubeOcupada) return;
    g_nubeOcupada = true;
    g_subNube = SubidaNube{};
    g_subNube.nombre = nombre;
    g_subNube.datos = datos;
    if ((long long)datos.size() > kParte) {
        g_subNube.partes = (int)(((long long)datos.size() + kParte - 1) / kParte);
        accion("nube", "Preparando la subida...", false);
        pedirApi("POST", "/partes", json{{"destino", "nube"}, {"nombre", nombre}, {"tam", (long long)datos.size()}}, [](int code, const json &b) {
            if (code != 201) { nubeFallo(code, b, "No se pudo subir el proyecto"); return; }
            g_subNube.subida = str(b, "subida");
            subirParteNube();
        });
        return;
    }
    std::string t = tmp("nube-subida");
    escribirPrivado(t, datos);
    accion("nube", "Subiendo a la nube...", false);
    Pet p;
    p.metodo = "PUT"; p.url = servidor::api() + "/nube/" + urlEnc(nombre); p.tipo = Tipo::Api; p.archivo = t;
    p.fin = [t](int code, const json &b) {
        std::error_code ec;
        fs::remove(fs::u8path(t), ec);
        if (code != 200) nubeFallo(code, b, "No se pudo subir el proyecto");
        else nubeHecha("Proyecto guardado en la nube.");
    };
    pedir(std::move(p));
}
void bajarNube(const std::string &nombre, const std::string &destino, std::function<void(bool)> fin) {
    if (!conectado() || g_nubeOcupada) { if (fin) fin(false); return; }
    g_nubeOcupada = true;
    accion("nube", "Descargando de la nube...", false);
    Pet p;
    p.metodo = "GET"; p.url = servidor::api() + "/nube/" + urlEnc(nombre); p.tipo = Tipo::Api; p.salida = destino;
    p.fin = [fin, destino](int code, const json &) {
        g_nubeOcupada = false;
        if (code != 200) {
            std::error_code ec;
            fs::remove(fs::u8path(destino), ec);
            accion("nube", code == 0 ? "Sin conexión con el servidor de Source 2D." : "No se pudo descargar el proyecto (" + std::to_string(code) + ").", true);
        } else accion("nube", "", false);
        if (fin) fin(code == 200);
    };
    pedir(std::move(p));
}
void borrarNube(const std::string &nombre) {
    if (!conectado() || g_nubeOcupada) return;
    g_nubeOcupada = true;
    pedirApi("DELETE", "/nube/" + urlEnc(nombre), json(), [](int code, const json &b) {
        if (code != 200) nubeFallo(code, b, "No se pudo borrar el proyecto");
        else nubeHecha("Proyecto borrado de la nube.");
    });
}

void meGusta(const std::string &clave, bool poner, std::function<void(long long, bool)> fin) {
    if (!conectado() || !claveSegura(clave)) { if (fin) fin(-1, false); return; }
    pedirApi(poner ? "POST" : "DELETE", "/megusta/" + clave, json(), [fin](int code, const json &b) {
        if (!fin) return;
        if (code == 200 && b.is_object()) fin(b.value("value", -1LL), b.value("mio", false));
        else fin(-1, false);
    });
}
void misMeGusta(const std::vector<std::string> &claves, std::function<void(const std::map<std::string, bool> &)> fin) {
    if (!conectado() || claves.empty()) return;
    std::string q;
    for (size_t i = 0; i < claves.size() && i < 100; i++) if (claveSegura(claves[i])) q += (q.empty() ? "" : ",") + claves[i];
    pedirApi("GET", "/megusta?claves=" + q, json(), [fin](int code, const json &b) {
        if (code != 200 || !b.is_object() || !fin) return;
        std::map<std::string, bool> r;
        for (auto &[k, v] : b.items()) if (v.is_boolean()) r[k] = v.get<bool>();
        fin(r);
    });
}
void contarDescarga(const std::string &clave, std::function<void(long long)> fin) {
    if (!claveSegura(clave)) return;
    Pet p;
    p.metodo = "POST"; p.url = servidor::api() + "/contador/" + clave; p.tipo = Tipo::Api; p.conToken = false;
    p.fin = [fin](int code, const json &b) { if (fin && code == 200 && b.is_object()) fin(b.value("value", -1LL)); };
    if (g_apiOn) pedir(std::move(p));
}

namespace {
std::vector<Conversacion> g_bandeja;
double g_bandejaAt = -1e9;
bool g_bandejaPidiendo = false;
struct Charla { std::vector<Mensaje> lista; double at = -1e9; bool pidiendo = false, cargada = false; };
std::map<std::string, Charla> g_charlas;
bool convSegura(const std::string &c) {
    size_t s = c.find("__");
    return s != std::string::npos && loginSeguro(c.substr(0, s)) && loginSeguro(c.substr(s + 2)) && c == minus(c);
}
Conversacion conversacionDe(const json &x) {
    Conversacion c;
    c.id = str(x, "conversacion"); c.con = str(x, "con"); c.ultimo = str(x, "ultimo"); c.estado = str(x, "estado"); c.solicitante = str(x, "solicitante");
    c.aviso = x.value("aviso", false);
    return c;
}
}
void pedirBandeja() {
    if (!conectado() || g_bandejaPidiendo || reloj() - g_bandejaAt < cada(15)) return;
    g_bandejaPidiendo = true;
    g_bandejaAt = reloj();
    pedirApi("GET", "/mensajes", json(), [](int code, const json &b) {
        g_bandejaPidiendo = false;
        if (code != 200 || !b.is_object() || !b.contains("conversaciones") || !b["conversaciones"].is_array()) return;
        std::vector<Conversacion> r;
        for (auto &x : b["conversaciones"]) if (x.is_object() && convSegura(str(x, "conversacion")) && loginSeguro(str(x, "con"))) r.push_back(conversacionDe(x));
        g_bandeja.swap(r);
    });
}
const std::vector<Conversacion> &bandeja() { return g_bandeja; }
void pedirConversacion(const std::string &conv) {
    if (!conectado() || !convSegura(conv)) return;
    Charla &c = g_charlas[conv];
    if (c.pidiendo || reloj() - c.at < cada(5)) return;
    c.pidiendo = true;
    c.at = reloj();
    pedirApi("GET", "/mensajes/" + conv, json(), [conv](int code, const json &b) {
        Charla &c = g_charlas[conv];
        c.pidiendo = false;
        if (code != 200 || !b.is_object()) return;
        c.cargada = true;
        std::vector<Mensaje> r;
        if (b.contains("mensajes") && b["mensajes"].is_array())
            for (auto &x : b["mensajes"]) if (x.is_object()) r.push_back({str(x, "id"), str(x, "de"), str(x, "cifrado"), str(x, "fecha")});
        c.lista.swap(r);
        if (b.contains("estado") && b["estado"].is_object())
            for (auto &cv : g_bandeja) if (cv.id == conv) { cv.estado = str(b["estado"], "estado"); cv.solicitante = str(b["estado"], "de"); }
    });
}
const std::vector<Mensaje> *mensajes(const std::string &conv) {
    auto it = g_charlas.find(conv);
    return it == g_charlas.end() || !it->second.cargada ? nullptr : &it->second.lista;
}
void enviarMensaje(const std::string &para, const std::string &cifrado) {
    if (!conectado() || !loginSeguro(para)) return;
    accion("mensaje", "Enviando...", false);
    pedirApi("POST", "/mensajes/" + para, json{{"cifrado", cifrado}}, [para](int code, const json &b) {
        if (code != 201) { accion("mensaje", error(code, b, "No se pudo enviar el mensaje"), true); return; }
        accion("mensaje", str(b, "estado") == "solicitud" ? "Solicitud enviada: podrás escribir más cuando la acepte." : "", false);
        std::string a = minus(g_user), c = minus(para);
        std::string conv = a < c ? a + "__" + c : c + "__" + a;
        g_charlas[conv].at = -1e9;
        g_bandejaAt = -1e9;
        pedirConversacion(conv);
        pedirBandeja();
    });
}
void responderSolicitud(const std::string &conv, bool aceptar) {
    if (!conectado() || !convSegura(conv)) return;
    pedirApi("POST", "/mensajes/" + conv + (aceptar ? "/aceptar" : "/bloquear"), json::object(), [conv](int code, const json &b) {
        if (code != 200) { accion("mensaje", error(code, b, "No se pudo responder"), true); return; }
        for (auto &cv : g_bandeja) if (cv.id == conv) cv.estado = str(b, "estado");
    });
}

std::string estadoAccion(const std::string &a) { auto it = g_acciones.find(a); return it == g_acciones.end() ? "" : it->second; }
bool falloAccion(const std::string &a) { return g_fallos.count(a) > 0; }
void limpiarAccion(const std::string &a) { g_acciones.erase(a); g_fallos.erase(a); }
bool servidorActivo() { return g_apiOn; }
}
