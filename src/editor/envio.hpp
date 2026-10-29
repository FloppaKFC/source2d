// Source 2D - enviar un juego a Source Play con la cuenta de GitHub del creador.
// Entrar: el "device flow" de GitHub (como gh): el editor muestra un codigo, la persona lo escribe en github.com y
// acepta; el editor nunca ve su contrasena. Hace falta la OAuth App de Source 2D (kClientId en envio.cpp, o
// S2D_GITHUB_CLIENT_ID). El permiso que se pide es public_repo (crear su copia del repositorio de envios).
// Enviar: copia (fork) de FloppaKFC/source2d-envios en su cuenta, una rama nueva con envios/<usuario>/<id>/ y un pull
// request. Despues @floppa-1550 lo prueba con Controladores/Source-Play.sh y lo aprueba o lo rechaza con un comentario.
// El token queda en <config>/github.json (solo lo puede leer este usuario del sistema); "Salir" lo borra.
// Cuentas: entrar registra la cuenta en Source Play (pone una estrella en FloppaKFC/source2d-juegos: la lista de estrellas
// es la lista de cuentas y su fecha, cuando se unio). Seguir es el de GitHub. Los permisos que se piden son public_repo y
// user:follow.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "project.hpp"

namespace envio {
void iniciar(bool pruebas);
void tick();

// cuenta
bool conectado();
std::string usuario();
void entrar();                        // empieza el inicio de sesion (abre github.com/login/device)
bool entrando();                      // esperando a que la persona escriba el codigo
std::string codigo();                 // el codigo que tiene que escribir ("ABCD-1234")
std::string urlCodigo();
void salir();
std::string errorCuenta();

// envio: archivos = nombre -> bytes (info.json, portada.png, juego.source2d o juego.s2dplay + original.s2dcerrado)
struct Paquete {
    std::string id, nombre;
    std::map<std::string, std::string> archivos;
    // juegos: carpeta "envios" y titulo "Envío"; paquetes de Source Store: "sprites"/"Sprites" o "sonidos"/"Sonidos".
    // El revisor (Source-Play.sh, Source-Sprite.sh, Source-Sound.sh) mira solo los envios con su titulo.
    std::string carpeta = "envios", titulo = "Envío";
};
void enviar(const Paquete &p);
bool enviando();
std::string estadoEnvio();            // paso actual, o como acabo
bool envioFallo();

// perfiles (de GitHub): se piden al abrirlos y se guardan mientras el editor sigue abierto
struct Perfil {
    std::string login, nombre, bio, creado, avatar;          // avatar: archivo local ya descargado ("" si aun no)
    // seguidores y siguiendo: solo cuentas registradas en Source Play (-1 mientras se cargan)
    int seguidores = -1, siguiendo = -1;
    std::vector<std::string> seguidoresSP, siguiendoSP;
    int sigo = -1;                                           // -1 no se sabe (o sin sesion), 0 no, 1 si
    bool cargado = false, error = false;
    std::vector<std::string> listaSeguidores, listaSiguiendo; // las de GitHub enteras (hasta 1000)
    bool listas = false, seguidoresListos = false, siguiendoListos = false;
};
void pedirPerfil(const std::string &login);
const Perfil *perfil(const std::string &login);               // nullptr si no se pidio
void pedirListas(const std::string &login);
void seguir(const std::string &login, bool si);
int sigo(const std::string &login);                           // -1 no se sabe (o sin sesion, o soy yo), 0 no, 1 si
// cuentas registradas en Source Play (login, fecha AAAA-MM-DD en que se unio), de la mas nueva a la mas vieja
void pedirRegistrados();
const std::vector<std::pair<std::string, std::string>> &registrados();
std::string unido(const std::string &login);                  // fecha en que se unio ("" si no esta o aun no se sabe)

// denunciar un juego o una cuenta: abre una incidencia en source2d-envios (la ve @floppa-1550 en Source-Play.sh)
void denunciar(const std::string &titulo, const std::string &cuerpo);
std::string estadoDenuncia();                                 // "Enviando...", "Denuncia enviada", o el error
bool denunciaFallo();
void limpiarDenuncia();                                       // al abrir otra denuncia

// los envios de esta cuenta y como van
struct Mio { int numero = 0; std::string nombre, fecha, estado, motivo, url; };   // estado: revisando, aprobado, rechazado
const std::vector<Mio> &mios();
void mirarMios();                     // los vuelve a pedir (como mucho cada 60 s)
}
