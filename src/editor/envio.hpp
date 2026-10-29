#pragma once
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
    std::string carpeta = "envios", titulo = "Envío";
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
};
void pedirPerfil(const std::string &login);
const Perfil *perfil(const std::string &login);
void pedirListas(const std::string &login);
void seguir(const std::string &login, bool si);
int sigo(const std::string &login);
void pedirRegistrados();
const std::vector<std::pair<std::string, std::string>> &registrados();
std::string unido(const std::string &login);

void denunciar(const std::string &titulo, const std::string &cuerpo);
std::string estadoDenuncia();
bool denunciaFallo();
void limpiarDenuncia();

struct Mio { int numero = 0; std::string nombre, fecha, estado, motivo, url; };
const std::vector<Mio> &mios();
void mirarMios();
}
