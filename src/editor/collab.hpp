// Source 2D - trabajar juntos en el mismo proyecto, en tiempo real y sin servidores.
// Una PC comparte (anfitrion) y las demas se conectan directo a ella por TCP. Da igual como se vean: la misma red
// WiFi, una zona WiFi que crea una de las PCs, o Tailscale; todo es una direccion IP.
// En la misma red las sesiones se encuentran solas (aviso por UDP). Para entrar hace falta el codigo de la sesion.
//
// Sincronizacion: cada pocas decimas se compara el proyecto con la ultima copia enviada (diff JSON, RFC 6902) y se
// manda la diferencia. El anfitrion pone el orden: aplica cada cambio y lo reenvia a todos, tambien a quien lo hizo,
// asi todos acaban igual (si dos cambian lo mismo a la vez, gana el ultimo que llega al anfitrion). Cada pocos
// segundos el anfitrion manda una huella del proyecto; si alguien no coincide, recibe el proyecto entero.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "project.hpp"

namespace collab {

enum class Role { None, Host, Client };

// otra persona en la sesion: que tiene abierto (doc, por ejemplo "scene:sc_x" o el nombre de la pestana), donde esta
// su cursor en ese documento (coordenadas del mundo) y que tiene seleccionado
struct Peer {
    int id = 0;
    std::string name, doc;
    uint32_t color = 0;                 // IM_COL32
    float x = 0, y = 0;
    bool cursor = false;
    std::vector<std::string> sel;
    json extra;                         // lo propio de cada editor: en eventos, el evento bajo el cursor ("at"); en un
                                        // sprite, el fotograma ("f") y el trazo que esta pintando ("paint")
};
// sesion vista en la red local (aviso del anfitrion)
struct Found { std::string addr, host, project; int port = 0; double seen = 0; };
// direccion de este equipo y por donde sale: "WiFi", "Zona WiFi", "Tailscale", "Cable" u "Otra"
struct NetAddr { std::string ip, iface, kind; };

// lo que paso en update() y el editor tiene que atender
struct Events {
    bool docReplaced = false;           // llego el proyecto entero (al entrar o al resincronizar)
    bool changed = false;               // se aplicaron cambios de otros
    std::set<std::string> sprites;      // ids de sprites que cambiaron (para recargar sus editores)
    bool textures = false;              // cambiaron sprites, tiles o fuentes (invalida texturas)
    std::string notice;                 // mensaje para el usuario ("Ana se unio", "Se perdio la conexion"...)
    bool noticeErr = false;
    bool ended = false;                 // la sesion termino (el anfitrion la cerro o se perdio la conexion)
};

constexpr int kPort = 47610;            // TCP de las sesiones (y +1 para el aviso por UDP)

Role role();
// empieza a compartir el proyecto; devuelve false y err si no se pudo (puerto ocupado...). El codigo se genera aqui.
bool host(Project &proj, const std::string &name, uint32_t color, std::string &err);
// se conecta a una sesion (addr: IP o nombre, con :puerto opcional); el proyecto llega despues por update()
bool join(const std::string &addr, const std::string &code, const std::string &name, uint32_t color, std::string &err);
void stop();
// cada fotograma: manda los cambios propios, aplica los ajenos y mueve la presencia
void update(Project &proj, Events &ev);
// lo que este usuario esta mirando (cursor en coordenadas del mundo del documento)
void setPresence(const std::string &doc, float x, float y, bool cursor, const std::vector<std::string> &sel, const json &extra = json());
// al final de cada fotograma: si ningun editor puso la presencia, solo se dice que pestana esta delante
void defaultPresence(const std::string &doc);

const std::vector<Peer> &peers();       // los demas (sin uno mismo)
const std::string &code();              // codigo de la sesion (anfitrion)
std::string status();                   // "Conectando...", "Conectado a ...", "Compartiendo"...
bool connected();                       // cliente: ya llego el proyecto

// buscar sesiones en la red local (escucha los avisos mientras este activo)
void discover(bool on);
std::vector<Found> found();
std::vector<NetAddr> localAddrs();

// ayuda para la zona WiFi en Linux (NetworkManager): crea o apaga la zona; out = mensaje para el usuario
bool hotspotStart(std::string &ssid, std::string &pass, std::string &out);
bool hotspotStop(std::string &out);

// si el cortafuegos del sistema bloquea las sesiones, como abrirlo ('' si no hace falta)
std::string firewallHint();
uint32_t colorFor(int i);

// copias: lo decide el anfitrion (apagado al empezar). Si esta apagado, el invitado no puede guardar ni exportar y, al
// terminar la sesion (se va el, se va el anfitrion o se corta), el editor cierra el proyecto y borra lo que quedo.
void setAllowCopies(bool on);
bool allowCopies();               // colores de las personas
std::string defaultName();              // nombre del usuario del sistema
}
