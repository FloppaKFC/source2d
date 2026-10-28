// Source 2D - noticias y actualizaciones (el "lanzador"): el editor baja noticias.json y versiones.json del repositorio
// publico, comprueba que vienen firmados por @floppa-1550 y los muestra en Inicio > Noticias. Las versiones nuevas se
// descargan a la carpeta de versiones y se abren desde el selector, como en el lanzador de Minecraft.
// El editor no envia nada: solo descarga archivos publicos con el curl del sistema.
#pragma once
#include <string>
#include <vector>

namespace novedades {
struct Noticia {
    int id = 0;
    std::string titulo, texto, tipo, fecha, enlace, version;   // tipo: aviso, evento, actualizacion, consejo
    std::string imagen;                                         // archivo local ya comprobado ("" si no hay o falta)
    bool fijada = false, importante = false;
};
struct Version {
    std::string nombre, canal, fecha, notas, url, blake2b;      // url y huella del ejecutable de este sistema
    int num = 0;
    long long tam = 0;
};
// buscar: Opciones > buscar noticias; versionsDir: <config>/versiones; pruebas: las pruebas automaticas (sin red salvo
// que S2D_NOVEDADES_URL diga otra cosa)
void iniciar(bool buscar, const std::string &versionsDir, bool pruebas);
void tick();                                     // cada fotograma: mira las descargas y vuelve a buscar cada 3 h
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
// descargas de versiones: progreso de 0 a 1, -1 si no se esta bajando
void descargar(const Version &v);
float progreso(const std::string &nombre);
std::string errorDescarga(const std::string &nombre);
bool instalada(const std::string &nombre);
}
