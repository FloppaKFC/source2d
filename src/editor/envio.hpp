#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "project.hpp"

namespace envio {
void iniciar(bool pruebas);
void tick();

bool conectado();
std::string usuario();
void entrar();
bool entrando();
std::string codigo();
std::string urlCodigo();
void salir();
std::string errorCuenta();

struct Paquete {
    std::string id, nombre;
    std::map<std::string, std::string> archivos;
    std::string carpeta = "juegos", titulo = "Envío";
};
void enviar(const Paquete &p);
bool enviando();
std::string estadoEnvio();
bool envioFallo();

struct Perfil {
    std::string login, nombre, bio, creado, avatar;
    int seguidores = -1, siguiendo = -1;
    std::vector<std::string> seguidoresSP, siguiendoSP;
    int sigo = -1;
    bool cargado = false, error = false;
    std::vector<std::string> listaSeguidores, listaSiguiendo;
    bool listas = false, seguidoresListos = false, siguiendoListos = false;
    bool spCargado = false;
    std::string frase, color, empezar, insignia, clavePublica;
    std::map<std::string, std::string> destacados;
};
void pedirPerfil(const std::string &login);
void refrescarPerfil(const std::string &login);
const Perfil *perfil(const std::string &login);
void pedirListas(const std::string &login);
void seguir(const std::string &login, bool si);
int sigo(const std::string &login);
void pedirRegistrados();
const std::vector<std::pair<std::string, std::string>> &registrados();
std::string unido(const std::string &login);
bool esBeta(const std::string &login);
void guardarPerfil(const json &campos);

struct Denuncia {
    std::string tipo = "juego", objetivo, titulo, cuerpo, conversacion;
    json mensajes = json::array();
};
void denunciar(const Denuncia &d);
void denunciar(const std::string &titulo, const std::string &cuerpo);
std::string estadoDenuncia();
bool denunciaFallo();
void limpiarDenuncia();

struct Mio { std::string id, tipo, nombre, fecha, estado, motivo, url; };
const std::vector<Mio> &mios();
void mirarMios();

struct Yo {
    bool listo = false, registrado = false, beta = false, baneado = false;
    std::string unido, baneoHasta, baneoMotivo;
    int grandeMb = 0, nubeMb = 0, sinLeer = 0;
};
const Yo &yo();

struct Notificacion { std::string id, tipo, titulo, texto, enlace, fecha; bool leida = false; };
void pedirNotificaciones();
const std::vector<Notificacion> &notificaciones();
bool notificacionesListas();
void marcarLeidas();

struct Comentario { std::string id, autor, texto, fecha; };
void pedirComentarios(const std::string &tipo, const std::string &id);
const std::vector<Comentario> *comentarios(const std::string &tipo, const std::string &id);
void comentar(const std::string &tipo, const std::string &id, const std::string &texto);
void darFeedback(const std::string &tipo, const std::string &id, const std::string &texto);

struct Feedback { std::string id, de, tipo, juego, nombre, texto, fecha; };
void pedirFeedback();
const std::vector<Feedback> &feedbackRecibido();

struct Solicitud { std::string id, tipo, motivo, proyecto, estado, respuesta, fecha; int mb = 0; };
void solicitar(const std::string &tipo, int mb, const std::string &motivo, const std::string &proyecto);
void pedirSolicitudes();
const std::vector<Solicitud> &solicitudes();

struct ProyectoNube { std::string nombre, fecha; long long tam = 0; };
void pedirNube();
bool nubeLista();
long long nubeCupo();
long long nubeUsado();
const std::vector<ProyectoNube> &nube();
void subirNube(const std::string &nombre, const std::string &datos);
void bajarNube(const std::string &nombre, const std::string &destino, std::function<void(bool)> fin);
void borrarNube(const std::string &nombre);
bool nubeOcupada();

void meGusta(const std::string &clave, bool poner, std::function<void(long long, bool)> fin);
void misMeGusta(const std::vector<std::string> &claves, std::function<void(const std::map<std::string, bool> &)> fin);
void contarDescarga(const std::string &clave, std::function<void(long long)> fin);

std::string estadoAccion(const std::string &accion);
bool falloAccion(const std::string &accion);
void limpiarAccion(const std::string &accion);
bool servidorActivo();
}
