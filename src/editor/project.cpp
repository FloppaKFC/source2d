#include "project.hpp"
#include "exporter.hpp"
#include "tiles_obj.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

std::string makeUid(const char *prefix) {
    static std::mt19937 rng{std::random_device{}()};
    static const char *abc = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string s = std::string(prefix) + "_";
    for (int i = 0; i < 6; i++) s += abc[rng() % 36];
    return s;
}

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

Px parseColor(const std::string &s) {
    Px p;
    if (s.empty() || s == "transparent") return p;
    if (s[0] == '#') {
        if (s.size() == 4 || s.size() == 5) {
            p.r = hexv(s[1]) * 17; p.g = hexv(s[2]) * 17; p.b = hexv(s[3]) * 17;
            p.a = s.size() == 5 ? hexv(s[4]) * 17 : 255;
        } else if (s.size() >= 7) {
            p.r = hexv(s[1]) * 16 + hexv(s[2]); p.g = hexv(s[3]) * 16 + hexv(s[4]); p.b = hexv(s[5]) * 16 + hexv(s[6]);
            p.a = s.size() >= 9 ? hexv(s[7]) * 16 + hexv(s[8]) : 255;
        }
        return p;
    }
    if (s.rfind("rgb", 0) == 0) {
        double v[4] = {0, 0, 0, 1};
        size_t a = s.find('(');
        if (a == std::string::npos) return p;
        std::string body = s.substr(a + 1);
        for (char &c : body) if (c == ',' || c == ')' || c == '/') c = ' ';
        std::istringstream in(body);
        for (int i = 0; i < 4 && (in >> v[i]); i++) {}
        p.r = (uint8_t)v[0]; p.g = (uint8_t)v[1]; p.b = (uint8_t)v[2];
        p.a = (uint8_t)(v[3] <= 1.0 ? v[3] * 255 + 0.5 : v[3]);
    }
    return p;
}

std::string colorToString(Px c) {
    if (c.a == 0) return "";
    char buf[16];
    if (c.a == 255) snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
    else snprintf(buf, sizeof buf, "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    return buf;
}

static void migrateSingleScene(json &d) {
    if (!d["scenes"].is_array() || d["scenes"].size() != 1 || !d["objects"].is_array()) return;
    json &sc = d["scenes"][0];
    if (sc.contains("events")) return;
    for (auto &o : d["objects"]) if (o.contains("scene")) return;
    std::string sid = sc.value("id", "");
    for (auto &o : d["objects"]) o["scene"] = sid;
    sc["events"] = d.contains("events") && d["events"].is_array() ? d["events"] : json::array();
    d["events"] = json::array();
}

bool Project::load(const std::string &file, std::string &err) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { err = "No se pudo abrir " + file; return false; }
    try {
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        json d;
        if (exporter::isSourcePack(bytes)) { if (!exporter::sourceUnpack(bytes, d, err)) return false; }
        else d = json::parse(bytes);
        if (!d.is_object() || !d.contains("sprites") || !d.contains("objects") || !d.contains("scenes")) {
            err = "El archivo no es un proyecto de Source 2D";
            return false;
        }
        migrateSingleScene(d);
        migrateSceneTilemaps(d);
        doc = std::move(d);
    } catch (const std::exception &e) {
        err = std::string("JSON no válido: ") + e.what();
        return false;
    }
    path = file;
    dirty = false;
    return true;
}

bool Project::save(const std::string &file, std::string &err) {
    std::string tmp = file + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) { err = "No se pudo escribir " + file; return false; }
        out << doc.dump(2);
        if (!out) { err = "Error al escribir " + file; return false; }
    }
    if (std::rename(tmp.c_str(), file.c_str()) != 0) { err = "No se pudo reemplazar " + file; return false; }
    path = file;
    dirty = false;
    return true;
}

void Project::newBlank(const std::string &nm) {
    doc = json::object();
    doc["id"] = makeUid("p");
    doc["name"] = nm;
    doc["version"] = 3;
    doc["settings"] = {{"width", 960}, {"height", 540}, {"fullscreen", true}, {"autofullscreen", false}};
    doc["globals"] = json::array();
    doc["sprites"] = json::array();
    doc["tilesets"] = json::array();
    doc["objects"] = json::array();
    json scene = {{"id", makeUid("sc")}, {"name", "Escena 1"}, {"width", 960}, {"height", 540}, {"background", "#202329"},
                  {"instances", json::array()}, {"tilemaps", json::array()}, {"effects", json::array()}};
    doc["scenes"] = json::array({scene});
    doc["events"] = json::array();
    path.clear();
    dirty = true;
}

std::string Project::name() const {
    if (doc.is_object() && doc.contains("name") && doc["name"].is_string()) return doc["name"].get<std::string>();
    return "Proyecto";
}

json &Project::sprites() {
    if (!doc.contains("sprites") || !doc["sprites"].is_array()) doc["sprites"] = json::array();
    return doc["sprites"];
}

json *Project::spriteById(const std::string &id) {
    for (auto &s : sprites()) if (s.value("id", "") == id) return &s;
    return nullptr;
}
