// Source 2D - Source Store: paquetes de sprites (sprites, tiles, autotiles) y de sonidos (musica y efectos hechos en
// Source Sound) para usar en los proyectos. Funciona como Source Play (tienda.hpp): cada tienda tiene su repositorio
// publico con catalogo.json firmado por @floppa-1550 (FloppaKFC/source2d-sprites y FloppaKFC/source2d-sonidos) y los
// paquetes van como archivos de sus Releases. Cada archivo se comprueba con la huella del catalogo.
// Un paquete (.s2dpaquete) es un JSON {"formato": "source2d-paquete", "tipo": "sprites"|"sonidos", ...} comprimido con
// LZMA como un .source2d (exporter::sourcePack), sin perder nada:
//   sprites:  "sprites", "tilesets" y "autotiles" con el mismo formato que dentro de un proyecto
//   sonidos:  "sonidos": canciones y efectos de Source Sound (notas, no audio grabado: ocupan muy poco)
// Lo descargado queda en <config>/source-store/<tipo>/.
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
    int nSprites = 0, nTiles = 0, nAutotiles = 0, nSonidos = 0;   // lo que trae (lo dice el catalogo)
};
void iniciar(bool buscar, bool pruebas);
void tick();
void buscarAhora(Tipo t);
std::string estado(Tipo t);
const std::vector<Paquete> &paquetes(Tipo t);
const Paquete *buscar(Tipo t, const std::string &id);

bool instalado(Tipo t, const std::string &id);
bool descargando(Tipo t, const std::string &id);
std::string error(Tipo t, const std::string &id);
void descargar(Tipo t, const Paquete &p);
void borrar(Tipo t, const std::string &id);
bool leer(Tipo t, const std::string &id, json &paquete, std::string &err);   // el paquete descargado, comprobado

// contadores (como Source Play): descargas y me gusta
void mirarContadores(Tipo t);
long long descargas(Tipo t, const std::string &id);
long long meGusta(Tipo t, const std::string &id);
bool leGusta(Tipo t, const std::string &id);
void darMeGusta(Tipo t, const Paquete &p);

// paquetes: comprobar uno (tipo, que solo tenga lo que toca) y meterlo en un proyecto con ids nuevos (sin pisar nada)
bool valido(const json &paquete, Tipo t, std::string &err);
int anadirAlProyecto(Project &proj, const json &paquete);   // cuantas cosas se anadieron
std::string pack(const json &paquete);                       // .s2dpaquete (LZMA)
bool unpack(const std::string &bytes, json &paquete, std::string &err);
}
