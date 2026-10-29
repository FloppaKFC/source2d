#pragma once
#include <string>
#include "project.hpp"

namespace cerrado {
constexpr size_t kMinClave = 10;
bool claveValida(const std::string &pass, std::string &err);

std::string lockPlay(const std::string &plain, std::string &keyHex);
bool unlockPlay(const std::string &data, const std::string &keyHex, std::string &plain);
bool isPlay(const std::string &data);

std::string lockSource(const std::string &plain, const std::string &pass);
bool unlockSource(const std::string &data, const std::string &pass, std::string &plain);
bool isLocked(const std::string &data);

json playCopy(const json &doc);
}
