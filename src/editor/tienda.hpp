#pragma once
#include <string>
#include <vector>
#include "project.hpp"

namespace tienda {
struct Archivo { std::string ruta, url, blake2b; long long tam = 0; };
struct Juego {
    std::string id, nombre, autor, descripcion, categoria, fecha, motor, contador;
    int version = 0;
    bool abierto = true;
    bool destacado = false;
    std::string llave;
    Archivo juego, portada, fuente;
    std::string portadaLocal;
};
enum class Estado { NoInstalado, Descargando, Instalado, Actualizable };

void iniciar(bool buscar, bool pruebas);
void tick();
void buscarAhora();
std::string estado();
const std::vector<Juego> &juegos();
std::vector<std::string> categorias();

Estado estadoDe(const Juego &j);
int versionInstalada(const std::string &id);
float progreso(const std::string &id);
std::string error(const std::string &id);
void descargar(const Juego &j);
void borrar(const std::string &id);
bool leer(const std::string &id, json &doc, std::string &err);
std::vector<std::string> instaladosSinCatalogo();
std::string nombreInstalado(const std::string &id);

void pedirFuente(const Juego &j);
bool fuenteLista(const std::string &id);
bool abrirFuente(const std::string &id, const std::string &clave, json &doc, std::string &err);

void mirarContadores();
long long descargas(const std::string &id);
long long meGusta(const std::string &id);
bool leGusta(const std::string &id);
bool favorito(const std::string &id);
void setFavorito(const std::string &id, bool si);
bool darMeGusta(const Juego &j, bool poner = true);

int insignia(const std::string &login);
std::vector<int> insignias(const std::string &login);
std::vector<std::string> contribuciones(const std::string &login);
std::vector<const Juego *> juegosDe(const std::string &login);
}
