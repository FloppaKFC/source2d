#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "json.hpp"

using json = nlohmann::ordered_json;

struct Px { uint8_t r = 0, g = 0, b = 0, a = 0; };
inline bool operator==(const Px &x, const Px &y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; }
inline bool operator!=(const Px &x, const Px &y) { return !(x == y); }

struct Project {
    json doc;
    std::string path;
    bool dirty = false;
    int rev = 0;

    bool load(const std::string &file, std::string &err);
    bool save(const std::string &file, std::string &err);
    void newBlank(const std::string &name);
    std::string name() const;
    json &sprites();
    json *spriteById(const std::string &id);
};

std::string makeUid(const char *prefix);
Px parseColor(const std::string &s);
std::string colorToString(Px c);
