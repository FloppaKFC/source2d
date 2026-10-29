// Source 2D - juegos cerrados de Source Play (el creador no deja "Ver por dentro").
// Un juego cerrado va en dos partes:
//   copia para jugar  "S2DPLAY1" + nonce(24) + mac(16) + cifrado   lo justo para jugar (sin comentarios ni capas de
//                     dibujo), con una llave aleatoria de ese juego. El editor la descifra en memoria y se la pasa al
//                     jugador por la entrada estandar: nunca queda un archivo legible en el disco.
//   proyecto original "S2DLOCK1" + sal(16) + nonce(24) + mac(16) + cifrado   el proyecto entero, con la clave del
//                     creador (minimo 10 caracteres, Argon2id de 64 MB). Sin la clave no se abre en el editor.
// Cifrado XChaCha20-Poly1305 de Monocypher. Sal, nonce y llave salen del generador del sistema, asi que aunque dos
// creadores usen la misma clave (o el mismo cifre dos veces) los resultados no se parecen en nada; el mac detecta
// cualquier cambio.
#pragma once
#include <string>
#include "project.hpp"

namespace cerrado {
constexpr size_t kMinClave = 10;
bool claveValida(const std::string &pass, std::string &err);   // minimo 10 caracteres (no bytes)

std::string lockPlay(const std::string &plain, std::string &keyHex);   // keyHex: la llave nueva (64 cifras hex)
bool unlockPlay(const std::string &data, const std::string &keyHex, std::string &plain);
bool isPlay(const std::string &data);

std::string lockSource(const std::string &plain, const std::string &pass);   // "" si fallo (sin memoria o azar)
bool unlockSource(const std::string &data, const std::string &pass, std::string &plain);
bool isLocked(const std::string &data);

// el proyecto sin lo que solo sirve para editarlo: comentarios de eventos y capas de dibujo de los sprites
json playCopy(const json &doc);
}
