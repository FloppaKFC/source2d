// Source 2D - enviar un juego a Source Play con la cuenta de GitHub del creador.
// Entrar: el "device flow" de GitHub (como gh): el editor muestra un codigo, la persona lo escribe en github.com y
// acepta; el editor nunca ve su contrasena. Hace falta la OAuth App de Source 2D (kClientId en envio.cpp, o
// S2D_GITHUB_CLIENT_ID). El permiso que se pide es public_repo (crear su copia del repositorio de envios).
// Enviar: copia (fork) de FloppaKFC/source2d-envios en su cuenta, una rama nueva con envios/<usuario>/<id>/ y un pull
// request. Despues @floppa-1550 lo prueba con Controladores/Source-Play.sh y lo aprueba o lo rechaza con un comentario.
// El token queda en <config>/github.json (solo lo puede leer este usuario del sistema); "Salir" lo borra.
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
};
void enviar(const Paquete &p);
bool enviando();
std::string estadoEnvio();            // paso actual, o como acabo
bool envioFallo();

// los envios de esta cuenta y como van
struct Mio { int numero = 0; std::string nombre, fecha, estado, motivo, url; };   // estado: revisando, aprobado, rechazado
const std::vector<Mio> &mios();
void mirarMios();                     // los vuelve a pedir (como mucho cada 60 s)
}
