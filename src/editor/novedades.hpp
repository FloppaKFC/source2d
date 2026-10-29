// Source 2D - noticias y actualizaciones (el "lanzador"): el editor baja noticias.json y versiones.json del repositorio
// publico, comprueba que vienen firmados por @floppa-1550 y los muestra en Inicio > Noticias. Las versiones nuevas se
// descargan a la carpeta de versiones y se abren desde el selector, como en el lanzador de Minecraft.
// El editor no envia nada de la persona: solo descarga archivos publicos con el curl del sistema. Lo unico que sale es el
// voto de una encuesta: "+1 a la opcion 2 de la encuesta n7-..." a un contador publico (abacus.jasoncameron.dev).
#pragma once
#include <string>
#include <vector>

namespace novedades {
struct Encuesta {
    std::string pregunta, clave;                                // clave: la del contador (de la noticia firmada)
    std::vector<std::string> opciones;
    long long cierra = 0;                                       // instante unix en que se cierra; 0 = nunca
};
struct Noticia {
    int id = 0;
    std::string titulo, texto, tipo, fecha, enlace, version;   // tipo: aviso, evento, actualizacion, consejo
    std::string inferior;                                       // texto debajo de la imagen
    std::string imagen;                                         // archivo local ya comprobado ("" si no hay o falta)
    bool fijada = false, importante = false;
    bool notificar = false;                                     // sale como notificacion arriba al llegar
    bool hayEncuesta = false;                                   // {encuesta} en el texto marca donde sale
    Encuesta encuesta;
};
struct Version {
    std::string nombre, canal, fecha, notas, url, blake2b;      // url y huella del ejecutable de este sistema
    int num = 0;
    long long tam = 0;
};
// buscar: Opciones > buscar noticias; versionsDir: <config>/versiones; pruebas: las pruebas automaticas (sin red salvo
// que S2D_NOVEDADES_URL diga otra cosa)
void iniciar(bool buscar, const std::string &versionsDir, bool pruebas);
void tick();                                     // cada fotograma: descargas, sondeo cada 5 s y busqueda cada 3 h
void buscarAhora();
void setBuscar(bool on);                         // Opciones: deja de buscar (o busca ya)
bool activo();                                   // false si este editor no tiene clave publica (compilado sin firma)
std::string estado();                            // "Buscando...", "Sin conexión...", "" si todo bien
const std::vector<Noticia> &noticias();          // las que tocan a esta version y sistema, fijadas primero
const std::vector<Version> &versiones();         // de versiones.json, de la mas nueva a la mas vieja
int sinLeer();
void marcarLeidas();
bool nueva(int id);                              // sin leer al abrir el editor (para la etiqueta "Nuevo")
const Noticia *importantePendiente();            // un anuncio importante que todavia no salio en ventana
void importanteVisto(int id);
const Noticia *notificacionPendiente();          // una noticia con "Notificar" que todavia no salio arriba
void notificada(int id);
// encuestas: mirarEncuesta cada fotograma que se ve (pide los votos cada 20 s); votos[i] = -1 si aun no se sabe
void mirarEncuesta(const Noticia &n);
std::vector<long long> votos(int id);
int miVoto(int id);                              // -1 si no voto desde este editor
void votar(const Noticia &n, int opcion);
bool encuestaCerrada(const Noticia &n);
std::string errorEncuesta(int id);
// descargas de versiones: progreso de 0 a 1, -1 si no se esta bajando
void descargar(const Version &v);
float progreso(const std::string &nombre);
std::string errorDescarga(const std::string &nombre);
bool instalada(const std::string &nombre);
}
