#pragma once
#include <string>
#include <vector>

namespace novedades {
struct Encuesta {
    std::string pregunta, clave;
    std::vector<std::string> opciones;
    long long cierra = 0;
};
struct Noticia {
    int id = 0;
    std::string titulo, texto, tipo, fecha, enlace, version;
    std::string inferior;
    std::string imagen;
    bool fijada = false, importante = false;
    bool notificar = false;
    bool hayEncuesta = false;
    Encuesta encuesta;
};
struct Version {
    std::string nombre, canal, fecha, notas, url, blake2b;
    int num = 0;
    long long tam = 0;
};
void iniciar(bool buscar, const std::string &versionsDir, bool pruebas);
void tick();
void buscarAhora();
void setBuscar(bool on);
bool activo();
std::string estado();
const std::vector<Noticia> &noticias();
const std::vector<Version> &versiones();
int sinLeer();
void marcarLeidas();
bool nueva(int id);
const Noticia *importantePendiente();
void importanteVisto(int id);
const Noticia *notificacionPendiente();
void notificada(int id);
void mirarEncuesta(const Noticia &n);
std::vector<long long> votos(int id);
int miVoto(int id);
void votar(const Noticia &n, int opcion);
bool encuestaCerrada(const Noticia &n);
std::string errorEncuesta(int id);
void descargar(const Version &v);
float progreso(const std::string &nombre);
std::string errorDescarga(const std::string &nombre);
bool instalada(const std::string &nombre);
}
