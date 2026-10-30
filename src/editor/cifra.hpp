#pragma once
#include <string>

namespace cifra {
std::string base64(const std::string &bytes);
bool deBase64(const std::string &texto, std::string &bytes);
bool claves(const std::string &login, std::string &secreta, std::string &publica);
std::string publica64(const std::string &login);
std::string cifrar(const std::string &login, const std::string &suPublica64, const std::string &texto, std::string &llave64);
bool descifrar(const std::string &login, const std::string &suPublica64, const std::string &blob64, std::string &texto, std::string &llave64);
bool abrirConLlave(const std::string &blob64, const std::string &llave64, std::string &texto);
}
