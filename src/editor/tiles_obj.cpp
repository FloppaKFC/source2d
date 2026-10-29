#include "tiles_obj.hpp"
#include <algorithm>
#include <cmath>

bool isTileObj(const json *obj) { return obj && jstr(*obj, "type") == "tile"; }

float tileSizeOf(Project &proj, const json &obj) {
    if (proj.doc.contains("tilesets") && proj.doc["tilesets"].is_array())
        if (json *ts = findById(proj.doc["tilesets"], jstr(obj, "tilesetId"))) return std::max(1.0f, (float)jnum(*ts, "tileSize", 16));
    return 16;
}

static bool hasMap(const json &inst) { return inst.contains("tm") && inst["tm"].is_object() && inst["tm"].contains("data") && inst["tm"]["data"].is_array(); }

bool tileInstRect(Project &proj, const json &inst, const json &obj, TileRect &r) {
    float T = tileSizeOf(proj, obj);
    r.x = (float)jnum(inst, "x", 0); r.y = (float)jnum(inst, "y", 0);
    if (!hasMap(inst)) { r.w = r.h = T; return false; }
    r.w = (float)jnum(inst["tm"], "cols", 0) * T; r.h = (float)jnum(inst["tm"], "rows", 0) * T;
    return r.w > 0 && r.h > 0;
}

json &projectAutotiles(Project &proj) {
    if (!proj.doc.contains("autotiles") || !proj.doc["autotiles"].is_array()) proj.doc["autotiles"] = json::array();
    return proj.doc["autotiles"];
}

json *autotileOf(Project &proj, const json &obj) {
    if (!jbool(obj, "autotile", false) || !proj.doc.contains("autotiles") || !proj.doc["autotiles"].is_array()) return nullptr;
    return findById(proj.doc["autotiles"], jstr(obj, "autotileId"));
}

int autotilePick(const json &at, int mask) {
    if (!at.contains("tiles") || !at["tiles"].is_array() || at["tiles"].size() < 16) return -1;
    int t = at["tiles"][mask & 15].is_number() ? at["tiles"][mask & 15].get<int>() : -1;
    if (t < 0) t = at["tiles"][0].is_number() ? at["tiles"][0].get<int>() : -1;   // sin tile para esa forma: el suelto
    return t;
}

bool autotileHas(const json &at, int tile) {
    if (tile < 0 || !at.contains("tiles") || !at["tiles"].is_array()) return false;
    for (auto &v : at["tiles"]) if (v.is_number() && v.get<int>() == tile) return true;
    return false;
}

// recorta las filas y columnas vacias de los bordes (moviendo la esquina); false si no queda nada
static bool trimMap(json &inst, float T) {
    json &tm = inst["tm"];
    int cols = (int)jnum(tm, "cols", 0), rows = (int)jnum(tm, "rows", 0);
    json &d = tm["data"];
    int c0 = cols, c1 = -1, r0 = rows, r1 = -1;
    for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) {
        int i = r * cols + c;
        if (i < (int)d.size() && d[i].is_number() && d[i].get<int>() >= 0) { c0 = std::min(c0, c); c1 = std::max(c1, c); r0 = std::min(r0, r); r1 = std::max(r1, r); }
    }
    if (c1 < 0) { tm["cols"] = 0; tm["rows"] = 0; d = json::array(); return false; }
    if (c0 == 0 && r0 == 0 && c1 == cols - 1 && r1 == rows - 1) return true;
    int nc = c1 - c0 + 1, nr = r1 - r0 + 1;
    json nd = json::array();
    for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) nd.push_back(d[r * cols + c]);
    d = nd; tm["cols"] = nc; tm["rows"] = nr;
    inst["x"] = jnum(inst, "x", 0) + c0 * T; inst["y"] = jnum(inst, "y", 0) + r0 * T;
    return true;
}

// agranda el mapa para que la celda (c, r) quede dentro; devuelve la celda en las coordenadas nuevas
static void growTo(json &inst, float T, int &c, int &r) {
    json &tm = inst["tm"];
    int cols = (int)jnum(tm, "cols", 0), rows = (int)jnum(tm, "rows", 0);
    if (cols <= 0 || rows <= 0) {   // mapa vacio: empieza en esa celda
        inst["x"] = jnum(inst, "x", 0) + c * T; inst["y"] = jnum(inst, "y", 0) + r * T;
        tm["cols"] = 1; tm["rows"] = 1; tm["data"] = json::array({-1});
        c = r = 0;
        return;
    }
    int addL = std::max(0, -c), addT = std::max(0, -r), addR = std::max(0, c - cols + 1), addB = std::max(0, r - rows + 1);
    if (!addL && !addT && !addR && !addB) return;
    int nc = cols + addL + addR, nr = rows + addT + addB;
    json &d = tm["data"], nd = json::array();
    for (int rr = 0; rr < nr; rr++) for (int cc = 0; cc < nc; cc++) {
        int oc = cc - addL, orr = rr - addT, oi = orr * cols + oc;
        nd.push_back(oc >= 0 && orr >= 0 && oc < cols && orr < rows && oi < (int)d.size() ? d[oi] : json(-1));
    }
    d = nd; tm["cols"] = nc; tm["rows"] = nr;
    inst["x"] = jnum(inst, "x", 0) - addL * T; inst["y"] = jnum(inst, "y", 0) - addT * T;
    c += addL; r += addT;
}

bool tileInstPaint(Project &proj, json &inst, const json &obj, float wx, float wy, int tile) {
    float T = tileSizeOf(proj, obj);
    if (!inst.contains("tm") || !inst["tm"].is_object()) inst["tm"] = {{"cols", 0}, {"rows", 0}, {"data", json::array()}};
    json &tm = inst["tm"];
    if (!tm.contains("data") || !tm["data"].is_array()) tm["data"] = json::array();
    int c = (int)std::floor((wx - jnum(inst, "x", 0)) / T), r = (int)std::floor((wy - jnum(inst, "y", 0)) / T);
    int cols = (int)jnum(tm, "cols", 0), rows = (int)jnum(tm, "rows", 0);
    bool inside = c >= 0 && r >= 0 && c < cols && r < rows;
    if (tile < 0 && !inside) return cols > 0 && rows > 0;
    if (tile >= 0) growTo(inst, T, c, r);
    cols = (int)jnum(tm, "cols", 0); rows = (int)jnum(tm, "rows", 0);
    json &d = tm["data"];
    if ((int)d.size() != cols * rows) d.get_ref<json::array_t &>().resize(cols * rows, -1);
    json *at = autotileOf(proj, obj);
    if (!at) {
        d[r * cols + c] = tile;
    } else {
        // autotile: la celda y sus cuatro vecinas eligen su tile segun las vecinas que tambien son del autotile
        auto in = [&](int cc, int rr) { return cc >= 0 && rr >= 0 && cc < cols && rr < rows && d[rr * cols + cc].is_number() && autotileHas(*at, d[rr * cols + cc].get<int>()); };
        auto fix = [&](int cc, int rr) {
            if (!in(cc, rr)) return;
            int m = (in(cc, rr - 1) ? 1 : 0) | (in(cc + 1, rr) ? 2 : 0) | (in(cc, rr + 1) ? 4 : 0) | (in(cc - 1, rr) ? 8 : 0);
            d[rr * cols + cc] = autotilePick(*at, m);
        };
        d[r * cols + c] = tile < 0 ? -1 : autotilePick(*at, 0);
        fix(c, r); fix(c, r - 1); fix(c + 1, r); fix(c, r + 1); fix(c - 1, r);
    }
    return tile >= 0 ? true : trimMap(inst, T);
}

void drawTileInst(ImDrawList *dl, Project &proj, const json &inst, const json &obj, ImVec2 o, float zoom, ImVec2 va, ImVec2 vb, ImU32 tint) {
    if (!hasMap(inst)) return;
    std::string tsId = jstr(obj, "tilesetId");
    float T = tileSizeOf(proj, obj), ox = (float)jnum(inst, "x", 0), oy = (float)jnum(inst, "y", 0);
    const json &tm = inst["tm"], &data = tm["data"];
    int cols = (int)jnum(tm, "cols", 0), rows = (int)jnum(tm, "rows", 0);
    int c0 = std::max(0, (int)std::floor((va.x - ox) / T)), c1 = std::min(cols - 1, (int)std::floor((vb.x - ox) / T));
    int r0 = std::max(0, (int)std::floor((va.y - oy) / T)), r1 = std::min(rows - 1, (int)std::floor((vb.y - oy) / T));
    for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) {
        int idx = r * cols + c;
        if (idx >= (int)data.size() || !data[idx].is_number()) continue;
        int t = data[idx].get<int>();
        if (t < 0) continue;
        if (ui::PixTexture *tx = tileTex(proj, tsId, t))
            dl->AddImage(tx->id(), ImVec2(o.x + (ox + c * T) * zoom, o.y + (oy + r * T) * zoom),
                         ImVec2(o.x + (ox + (c + 1) * T) * zoom, o.y + (oy + (r + 1) * T) * zoom), ImVec2(0, 0), ImVec2(1, 1), tint);
    }
}

void starterTiles(json &ts) {
    int T = (int)jnum(ts, "tileSize", 16);
    if (!ts.contains("tiles") || !ts["tiles"].is_array() || ts["tiles"].size() < 2) return;
    // 0: cesped sobre tierra, 1: tierra (los dos solidos); manchas en un patron fijo
    auto speck = [](int x, int y) { return ((x * 7 + y * 13) % 11) == 0; };
    for (int k = 0; k < 2; k++) {
        json px = json::array();
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
            const char *c = "#8a5a3a";
            if (k == 0 && y < T / 4) c = y == 0 ? "#7fd35e" : "#5fb34a";
            else if (k == 0 && y == T / 4) c = "#3f7f34";
            else if (speck(x, y)) c = "#6e4630";
            px.push_back(c);
        }
        ts["tiles"][k] = {{"pixels", px}, {"solid", true}};
    }
}

void migrateSceneTilemaps(json &doc) {
    if (!doc.contains("scenes") || !doc["scenes"].is_array() || !doc.contains("objects") || !doc["objects"].is_array()) return;
    json &objs = doc["objects"];
    int made = 0;
    for (auto &sc : doc["scenes"]) {
        if (!sc.contains("tilemaps") || !sc["tilemaps"].is_array() || sc["tilemaps"].empty()) continue;
        std::string sid = sc.value("id", "");
        if (!sc.contains("instances") || !sc["instances"].is_array()) sc["instances"] = json::array();
        for (auto &tm : sc["tilemaps"]) {
            std::string tsId = jstr(tm, "tilesetId"), layer = jstr(tm, "layer", "base");
            if (layer.empty()) layer = "base";
            json *ts = doc.contains("tilesets") && doc["tilesets"].is_array() ? findById(doc["tilesets"], tsId) : nullptr;
            float T = ts ? std::max(1.0f, (float)jnum(*ts, "tileSize", 16)) : 16;
            // dueno: un objeto Tile de ese conjunto y esa capa (de la escena o global); si no hay, uno nuevo
            json *owner = nullptr;
            for (auto &o : objs)
                if (jstr(o, "type") == "tile" && jstr(o, "tilesetId") == tsId && jstr(o, "capa", "base") == layer &&
                    (jstr(o, "scene") == sid || !o.contains("scene")) && (!owner || jstr(o, "scene") == sid)) owner = &o;
            if (!owner) {
                std::string nm = jstr(tm, "name", "Mapa");
                nm.erase(std::remove(nm.begin(), nm.end(), ' '), nm.end());
                if (nm.empty()) nm = "Mapa";
                for (auto &o : objs) if (jstr(o, "name") == nm) { nm += std::to_string(++made); break; }
                objs.push_back({{"id", makeUid("o")}, {"name", nm}, {"type", "tile"}, {"tilesetId", tsId}, {"spriteId", nullptr}, {"capa", layer},
                                {"scene", sid}, {"hud", false}, {"shader", ""}, {"hooks", {{"onCreate", ""}, {"onUpdate", ""}}}, {"vars", json::array()},
                                {"animations", json::array()}, {"behaviors", json::object()}, {"comportamientos", json::object()}});
                owner = &objs.back();
            }
            std::string ownerId = jstr(*owner, "id");
            json inst = {{"id", makeUid("i")}, {"objectId", ownerId}, {"x", jnum(tm, "x", 0)}, {"y", jnum(tm, "y", 0)}, {"angle", 0}, {"scaleX", 1},
                         {"scaleY", 1}, {"opacity", 100}, {"frame", 0},
                         {"tm", {{"cols", jnum(tm, "cols", 0)}, {"rows", jnum(tm, "rows", 0)}, {"data", tm.contains("data") && tm["data"].is_array() ? tm["data"] : json::array()}}}};
            if (!jstr(tm, "shader").empty()) inst["tm"]["shader"] = jstr(tm, "shader");
            if (trimMap(inst, T)) sc["instances"].push_back(inst);
        }
        sc["tilemaps"] = json::array();
    }
}
