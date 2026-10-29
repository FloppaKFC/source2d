#pragma once
#include <string>
#include <vector>
#include "project.hpp"

namespace exporter {
bool available(const std::string &os, bool lite);
bool needsFull(const json &doc, std::string &why);
bool exportGame(const json &doc, const std::string &out, const std::string &os, bool lite, std::string &err, std::string &info);
bool exportFolder(const json &doc, const std::string &dir, const std::string &os, const std::string &name, std::string &err, std::string &info);
bool unpackPlayer(const std::string &os, std::vector<unsigned char> &out);
unsigned long playerSize(const std::string &os);
struct Packed { std::string json, soft, hard; };
Packed packed(const json &doc, bool pixels = true);
std::string sourcePack(const json &doc);
bool isSourcePack(const std::string &bytes);
bool sourceUnpack(const std::string &bytes, json &doc, std::string &err);
std::string kb(long bytes);
}
