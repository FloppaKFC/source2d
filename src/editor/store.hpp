#pragma once
#include <string>
#include <vector>
#include "project.hpp"
#include "tienda.hpp"

namespace store {
enum Tipo { Sprites = 0, Sonidos = 1 };
struct Paquete {
    std::string id, nombre, autor, descripcion, categoria, licencia, fecha, contador;
    int version = 0;
    bool destacado = false;
    tienda::Archivo archivo, portada;
    std::string portadaLocal;
    int nSprites = 0, nTiles = 0, nAutotiles = 0, nSonidos = 0;
};
void iniciar(bool buscar, bool pruebas);
void tick();
void buscarAhora(Tipo t);
void refrescar(Tipo t);
void pedirVista(Tipo t, const Paquete &p);
bool vista(Tipo t, const Paquete &p, json &pk);
std::string estado(Tipo t);
const std::vector<Paquete> &paquetes(Tipo t);
const Paquete *buscar(Tipo t, const std::string &id);

bool instalado(Tipo t, const std::string &id);
bool descargando(Tipo t, const std::string &id);
std::string error(Tipo t, const std::string &id);
void descargar(Tipo t, const Paquete &p);
void borrar(Tipo t, const std::string &id);
bool leer(Tipo t, const std::string &id, json &paquete, std::string &err);

void mirarContadores(Tipo t);
long long descargas(Tipo t, const std::string &id);
long long meGusta(Tipo t, const std::string &id);
bool leGusta(Tipo t, const std::string &id);
bool darMeGusta(Tipo t, const Paquete &p, bool poner = true);

bool valido(const json &paquete, Tipo t, std::string &err);
int anadirAlProyecto(Project &proj, const json &paquete);
std::string pack(const json &paquete);
bool unpack(const std::string &bytes, json &paquete, std::string &err);
}
