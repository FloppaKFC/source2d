// Source 2D - Source Play: la tienda de juegos (gratis) de la pestana de Inicio.
// Los archivos grandes (juegos, proyectos cerrados) van como archivos de Releases de source2d-juegos: gratis, hasta
// 2 GB cada uno y sin contar para el tamano del repositorio (en el repositorio solo queda el catalogo).
// El repositorio publico FloppaKFC/source2d-juegos tiene catalogo.json y catalogo.json.firma (firmado con la clave de
// @floppa-1550, como las noticias) y los juegos aprobados en juegos/<id>/v<version>/. El editor solo muestra lo que viene
// firmado; cada archivo (juego, portada, proyecto cerrado) se comprueba con la huella BLAKE2b que trae el catalogo.
// Lo descargado queda en <config>/source-play/: catalogo (para verlo sin internet), portadas y juegos instalados.
// Descargas y "me gusta" van al mismo contador publico que las encuestas (abacus.jasoncameron.dev).
#pragma once
#include <string>
#include <vector>
#include "project.hpp"

namespace tienda {
// ruta: el nombre del archivo (para su extension); url: de donde se baja. Los juegos van como archivos de una Release de
// source2d-juegos (no cuentan para el tamano del repositorio); "archivo" relativo al repositorio tambien vale
struct Archivo { std::string ruta, url, blake2b; long long tam = 0; };
struct Juego {
    std::string id, nombre, autor, descripcion, categoria, fecha, motor, contador;
    int version = 0;
    bool abierto = true;              // el creador deja "Ver por dentro"
    bool destacado = false;           // en la fila de Destacados (lo decide Source-Play.sh)
    std::string llave;                // cerrado: la llave de la copia para jugar
    Archivo juego, portada, fuente;   // fuente: el proyecto original cifrado con la clave del creador (solo cerrados)
    std::string portadaLocal;         // archivo ya comprobado ("" si aun no esta)
};
enum class Estado { NoInstalado, Descargando, Instalado, Actualizable };

void iniciar(bool buscar, bool pruebas);
void tick();
void buscarAhora();
std::string estado();                       // "Buscando...", "Sin conexion..." o ""
const std::vector<Juego> &juegos();
std::vector<std::string> categorias();

Estado estadoDe(const Juego &j);
int versionInstalada(const std::string &id);
float progreso(const std::string &id);      // 0..1 mientras se descarga, -1 si no
std::string error(const std::string &id);
void descargar(const Juego &j);
void borrar(const std::string &id);
// el proyecto instalado listo para el jugador (descifrado en memoria si es cerrado); false y err si no se puede
bool leer(const std::string &id, json &doc, std::string &err);
// instalados que ya no estan en el catalogo (el creador o Source Play los quitaron): siguen en la lista hasta borrarlos
std::vector<std::string> instaladosSinCatalogo();
std::string nombreInstalado(const std::string &id);

// "Ver por dentro" de un juego cerrado: baja el proyecto original y lo abre con la clave del creador
void pedirFuente(const Juego &j);
bool fuenteLista(const std::string &id);    // ya descargada y comprobada
bool abrirFuente(const std::string &id, const std::string &clave, json &doc, std::string &err);

// contadores (mirarContadores cada fotograma que se ve la seccion; se piden cada 60 s)
void mirarContadores();
long long descargas(const std::string &id);  // -1 si aun no se sabe
long long meGusta(const std::string &id);
bool leGusta(const std::string &id);
// favoritos de esta persona (en este equipo): salen arriba, en su propia fila
bool favorito(const std::string &id);
void setFavorito(const std::string &id, bool si);
void darMeGusta(const Juego &j);

// cuentas: cuentas.json (firmado como el catalogo, lo lleva Controladores/Source-Cuentas.sh) dice quien esta verificado y
// quien es admin. 0 nada, 1 verificada, 2 admin, 3 creador de Source 2D
int insignia(const std::string &login);
std::vector<const Juego *> juegosDe(const std::string &login);
}
