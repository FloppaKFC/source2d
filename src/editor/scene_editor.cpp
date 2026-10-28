#include "scene_editor.hpp"
#include "collab.hpp"
#include "code_editor.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

using ui::Icon;

json &projectLayers(Project &proj) {
    json &d = proj.doc;
    if (!d.contains("layers") || !d["layers"].is_array() || d["layers"].empty())
        d["layers"] = json::array({{{"id", "base"}, {"name", "Principal"}, {"z", 0}, {"parallax", 1}, {"solid", true}}});
    return d["layers"];
}

static const json *findLayer(Project &proj, const std::string &id) {
    if (proj.doc.contains("layers") && proj.doc["layers"].is_array())
        for (auto &L : proj.doc["layers"]) if (jstr(L, "id") == id) return &L;
    return nullptr;
}

static const json *layerOf(Project &proj, const json &obj) {
    std::string id = jstr(obj, "capa", "base");
    if (proj.doc.contains("layers") && proj.doc["layers"].is_array())
        for (auto &L : proj.doc["layers"]) if (jstr(L, "id") == id) return &L;
    return nullptr;
}

std::string SceneEditor::title(Project &proj) {
    json *sc = findById(proj.doc["scenes"], sceneId);
    return "Escena: " + (sc ? jstr(*sc, "name") : std::string("?")) + "###scn_" + sceneId;
}

float SceneEditor::snapV(float v, float g) const { return snap && g > 0 ? std::round(v / g) * g : std::round(v); }

// Los objetos HUD se colocan en pixeles de la pantalla del juego. Con la camara a zoom Z se ven en el mundo
// a 1/Z (posicion y escala), que es donde aparecen al jugar: k es ese factor (1 para el resto).
static float camZoom(const json &sc) {
    float z = sc.contains("camera") && sc["camera"].is_object() ? (float)jnum(sc["camera"], "zoom", 1) : 1;
    return z > 0 ? z : 1;
}
static float hudK(const json &sc, const json *obj) { return obj && jbool(*obj, "hud", false) ? 1 / camZoom(sc) : 1; }

/* ---------------- operaciones ---------------- */
std::string SceneEditor::placeAt(Project &proj, json &sc, float wx, float wy) {
    if (placeObj.empty() || !findById(proj.doc["objects"], placeObj)) return "";
    float gw = (float)jnum(sc, "gridW", 16), gh = (float)jnum(sc, "gridH", 16), k = hudK(sc, findById(proj.doc["objects"], placeObj));
    std::string id = makeUid("i");
    sc["instances"].push_back({{"id", id}, {"objectId", placeObj}, {"x", snapV(wx / k, gw)}, {"y", snapV(wy / k, gh)}, {"angle", 0},
                               {"scaleX", 1}, {"scaleY", 1}, {"opacity", 100}, {"frame", 0}});
    sel = {id};
    return id;
}

void SceneEditor::deleteSelected(json &sc) {
    json &arr = sc["instances"];
    for (size_t i = arr.size(); i-- > 0;)
        if (std::find(sel.begin(), sel.end(), jstr(arr[i], "id")) != sel.end()) arr.erase(arr.begin() + i);
    sel.clear();
}

void SceneEditor::duplicateSelected(json &sc) {
    json &arr = sc["instances"];
    std::vector<std::string> nsel;
    float off = (float)jnum(sc, "gridW", 16);
    for (auto &id : sel) {
        json *it = findById(arr, id);
        if (!it) continue;
        json c = *it;
        c["id"] = makeUid("i");
        c["x"] = jnum(c, "x", 0) + off; c["y"] = jnum(c, "y", 0) + off;
        nsel.push_back(jstr(c, "id"));
        arr.push_back(c);
    }
    sel = nsel;
}

void SceneEditor::moveSelected(json &sc, float dx, float dy) {
    for (auto &id : sel) if (json *it = findById(sc["instances"], id)) { (*it)["x"] = jnum(*it, "x", 0) + dx; (*it)["y"] = jnum(*it, "y", 0) + dy; }
}

static json *tilemapById(json &sc, const std::string &id) {
    if (!sc.contains("tilemaps")) sc["tilemaps"] = json::array();
    return findById(sc["tilemaps"], id);
}

// autotile: 16 tiles seguidos desde el elegido, en el orden de la mascara de vecinos iguales (arriba 1, derecha 2,
// abajo 4, izquierda 8). Al pintar o borrar se recalcula la celda y sus cuatro vecinas.
static bool g_autotile = false;
static int g_autoBase = -1;
void SceneEditor::paintCell(Project &proj, json &sc, float wx, float wy, int tile) {
    if (g_autotile && g_autoBase >= 0) {
        json *tm = tilemapById(sc, curTilemap);
        if (!tm) return;
        json *ts = findById(proj.doc["tilesets"], jstr(*tm, "tilesetId"));
        float T = ts ? (float)jnum(*ts, "tileSize", 16) : 16;
        int cols = (int)jnum(*tm, "cols", 0), rows = (int)jnum(*tm, "rows", 0);
        int c = (int)std::floor((wx - jnum(*tm, "x", 0)) / T), r = (int)std::floor((wy - jnum(*tm, "y", 0)) / T);
        if (c < 0 || r < 0 || c >= cols || r >= rows) return;
        json &data = (*tm)["data"];
        if (!data.is_array() || (int)data.size() != cols * rows) { data = json::array(); for (int i = 0; i < cols * rows; i++) data.push_back(-1); }
        int base = g_autoBase;
        auto in = [&](int cc, int rr) { if (cc < 0 || rr < 0 || cc >= cols || rr >= rows) return false; int v = data[rr * cols + cc].get<int>(); return v >= base && v < base + 16; };
        auto fix = [&](int cc, int rr) {
            if (!in(cc, rr)) return;
            int m = (in(cc, rr - 1) ? 1 : 0) | (in(cc + 1, rr) ? 2 : 0) | (in(cc, rr + 1) ? 4 : 0) | (in(cc - 1, rr) ? 8 : 0);
            data[rr * cols + cc] = base + m;
        };
        data[r * cols + c] = tile < 0 ? -1 : base;
        fix(c, r); fix(c, r - 1); fix(c + 1, r); fix(c, r + 1); fix(c - 1, r);
        return;
    }
    json *tm = tilemapById(sc, curTilemap);
    if (!tm) return;
    json *ts = findById(proj.doc["tilesets"], jstr(*tm, "tilesetId"));
    float T = ts ? (float)jnum(*ts, "tileSize", 16) : 16;
    int cols = (int)jnum(*tm, "cols", 0), rows = (int)jnum(*tm, "rows", 0);
    int c = (int)std::floor((wx - jnum(*tm, "x", 0)) / T), r = (int)std::floor((wy - jnum(*tm, "y", 0)) / T);
    if (c < 0 || r < 0 || c >= cols || r >= rows) return;
    json &data = (*tm)["data"];
    if (!data.is_array() || (int)data.size() != cols * rows) { data = json::array(); for (int i = 0; i < cols * rows; i++) data.push_back(-1); }
    data[r * cols + c] = tile;
}

void SceneEditor::addTilemap(Project &proj, json &sc) {
    if (!proj.doc.contains("tilesets") || proj.doc["tilesets"].empty()) return;
    json &ts = proj.doc["tilesets"][0];
    int T = (int)jnum(ts, "tileSize", 16);
    int cols = std::max(1, (int)std::ceil(jnum(sc, "width", 960) / T)), rows = std::max(1, (int)std::ceil(jnum(sc, "height", 540) / T));
    json data = json::array();
    for (int i = 0; i < cols * rows; i++) data.push_back(-1);
    std::string id = makeUid("tm");
    sc["tilemaps"].push_back({{"id", id}, {"name", "Mapa " + std::to_string(sc["tilemaps"].size() + 1)}, {"tilesetId", jstr(ts, "id")},
                              {"x", 0}, {"y", 0}, {"cols", cols}, {"rows", rows}, {"shader", ""}, {"data", data}});
    curTilemap = id;
}

static bool hitInstance(Project &proj, const json &inst, const json &obj, float wx, float wy, float k) {
    ObjSize bs = objBaseSize(proj, obj);
    float hw = k * bs.w * std::fabs((float)jnum(inst, "scaleX", 1)) / 2, hh = k * bs.h * std::fabs((float)jnum(inst, "scaleY", 1)) / 2;
    float x = k * (float)jnum(inst, "x", 0), y = k * (float)jnum(inst, "y", 0);
    if (jstr(obj, "type") == "text") {   // el texto se ancla a la izquierda, centro o derecha
        std::string al = obj.contains("text") ? jstr(obj["text"], "align", "left") : "left";
        if (al == "left") x += hw; else if (al == "right") x -= hw;
    }
    float a = -(float)jnum(inst, "angle", 0) * 3.14159265f / 180.0f, dx = wx - x, dy = wy - y;
    float lx = dx * std::cos(a) - dy * std::sin(a), ly = dx * std::sin(a) + dy * std::cos(a);
    return std::fabs(lx) <= std::max(hw, 3.0f) && std::fabs(ly) <= std::max(hh, 3.0f);
}

static bool layerHidden(Project &proj, const json &obj) {
    const json *L = layerOf(proj, obj);
    return L && jbool(*L, "hidden", false);
}

std::string SceneEditor::pickAt(Project &proj, json &sc, float wx, float wy) {
    json &arr = sc["instances"];
    std::string best; double bestKey = -1e18;
    for (size_t i = 0; i < arr.size(); i++) {
        json *obj = findById(proj.doc["objects"], jstr(arr[i], "objectId"));
        if (!obj || layerHidden(proj, *obj) || !hitInstance(proj, arr[i], *obj, wx, wy, hudK(sc, obj))) continue;
        const json *L = layerOf(proj, *obj);
        double key = (L ? jnum(*L, "z", 0) : 0) * 1e6 + jnum(arr[i], "z", 0) * 1e3 + (double)i * 1e-3;
        if (key > bestKey) { bestKey = key; best = jstr(arr[i], "id"); }
    }
    return best;
}

/* ---------------- interfaz ---------------- */
void SceneEditor::drawObjectsPanel(Project &proj, json &) {
    ImGui::TextUnformatted("Objetos");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Buscar objetos", search, sizeof search);
    ImGui::BeginChild("##olist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4));
    std::string q = search;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    for (auto &o : proj.doc["objects"]) {
        if (!objectInScene(o, sceneId)) continue;   // los objetos de esta escena y los globales
        std::string id = jstr(o, "id"), nm = jstr(o, "name"), lnm = nm;
        std::transform(lnm.begin(), lnm.end(), lnm.begin(), ::tolower);
        if (!q.empty() && lnm.find(q) == std::string::npos) continue;
        ImGui::PushID(id.c_str());
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool selected = mode == Place && placeObj == id;
        if (ImGui::Selectable("##o", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 34))) {
            placeObj = id; mode = Place;
            if (ImGui::IsMouseDoubleClicked(0) && onOpenObject) onOpenObject(id);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clic: colocar en la escena\nDoble clic: editar el objeto");
        objectThumbAt(ImGui::GetWindowDrawList(), proj, o, ImVec2(p.x + 2, p.y + 2), 30);
        std::string type = jstr(o, "type", "sprite");
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(p.x + 40, p.y + 1), IM_COL32(235, 235, 242, 255), nm.c_str());
        dl->AddText(ImVec2(p.x + 40, p.y + 17), ui::kDim, objTypeLabel(type));
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ui::primaryButton("+ Añadir un objeto", ImVec2(-1, 0)) && onNewObject) onNewObject();
}

void SceneEditor::drawToolbar(Project &, json &sc) {
    if (ui::iconButton("msel", Icon::Select, mode == Select, "Seleccionar y mover (S)", 28)) mode = Select;
    ImGui::SameLine();
    if (ui::iconButton("mplace", Icon::Plus, mode == Place, "Colocar objeto (P): clic izquierdo coloca, derecho borra", 28)) mode = Place;
    ImGui::SameLine();
    if (ui::iconButton("mtiles", Icon::Pencil, mode == Tiles, "Pintar tiles (T): izquierdo pinta, derecho borra", 28)) mode = Tiles;
    ImGui::SameLine(0, 16);
    if (ui::iconButton("grid", Icon::Grid, grid, "Mostrar la cuadrícula", 28)) grid = !grid;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Checkbox("Ajustar", &snap);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ajustar a la cuadrícula al colocar y mover (Alt lo desactiva al arrastrar)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60); editNum("##gw", sc, "gridW", 16, 0.2f, "%.0f", 1, 512);
    ImGui::SameLine(); ImGui::TextUnformatted("x"); ImGui::SameLine();
    ImGui::SetNextItemWidth(60); editNum("##gh", sc, "gridH", 16, 0.2f, "%.0f", 1, 512);
    ImGui::SameLine(0, 16);
    ImGui::Text("%d%%", (int)std::round(zoom * 100));
    ImGui::SameLine();
    if (ImGui::SmallButton("Ajustar vista")) { fitPending = true; userView = false; }
    ImGui::SameLine();
    if (ImGui::SmallButton("100%")) { zoom = 1; userView = true; }
    ImGui::SameLine(0, 16);
    if (!sel.empty()) {
        ImGui::Text("%d seleccionada(s)", (int)sel.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Duplicar")) duplicateSelected(sc);
        ImGui::SameLine();
        if (ImGui::SmallButton("Eliminar")) deleteSelected(sc);
        ImGui::SameLine();
    }
    float bw = 190;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - bw - 12));
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ui::primaryButton("     Probar esta escena", ImVec2(bw, 0)) && onPlayScene) onPlayScene(sceneId);
    ui::drawIcon(ImGui::GetWindowDrawList(), Icon::Play, ImVec2(p.x + 16, p.y + ImGui::GetFrameHeight() / 2), 11, IM_COL32_WHITE);
}

void SceneEditor::drawCanvas(Project &proj, json &sc) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 20 || avail.y < 20) return;
    canvasPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##scene", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float SW = (float)jnum(sc, "width", 960), SH = (float)jnum(sc, "height", 540);
    float VW = (float)jnum(proj.doc["settings"], "width", 960), VH = (float)jnum(proj.doc["settings"], "height", 540);
    if (!userView && (std::fabs(avail.x - lastAvail.x) > 1 || std::fabs(avail.y - lastAvail.y) > 1)) fitPending = true;
    lastAvail = avail;
    if (fitPending) {
        zoom = std::min((avail.x - 60) / std::max(SW, VW / camZoom(sc)), (avail.y - 60) / std::max(SH, VH / camZoom(sc)));
        zoom = std::clamp(zoom, 0.05f, 8.0f);
        panX = (avail.x - SW * zoom) / 2; panY = (avail.y - SH * zoom) / 2;
        fitPending = false;
    }
    if (hovered && io.MouseWheel != 0) {
        float old = zoom;
        userView = true;
        zoom = std::clamp(zoom * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), 0.05f, 16.0f);
        float mx = io.MousePos.x - canvasPos.x, my = io.MousePos.y - canvasPos.y;
        panX = mx - (mx - panX) * zoom / old; panY = my - (my - panY) * zoom / old;
    }
    ImVec2 mw = toWorld(io.MousePos);
    float gw = (float)jnum(sc, "gridW", 16), gh = (float)jnum(sc, "gridH", 16);
    json &insts = sc["instances"];
    if (!insts.is_array()) insts = json::array();

    /* ---- raton ---- */
    bool space = ImGui::IsKeyDown(ImGuiKey_Space) && hovered;
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (space && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))) drag = DPan;
    if (drag == DPan) {
        panX += io.MouseDelta.x; panY += io.MouseDelta.y; userView = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) drag = DNone;
    } else if (hovered && drag == DNone) {
        if (mode == Select) {
            // asas de escala de la seleccion unica
            int handle = -1;
            if (sel.size() == 1) {
                json *it = findById(insts, sel[0]);
                json *obj = it ? findById(proj.doc["objects"], jstr(*it, "objectId")) : nullptr;
                if (it && obj && jstr(*obj, "type") == "sprite") {
                    ObjSize bs = objBaseSize(proj, *obj);
                    float k = hudK(sc, obj);
                    float hw = k * bs.w * (float)jnum(*it, "scaleX", 1) / 2, hh = k * bs.h * (float)jnum(*it, "scaleY", 1) / 2;
                    ImVec2 c = toScreen(k * (float)jnum(*it, "x", 0), k * (float)jnum(*it, "y", 0));
                    float a = (float)jnum(*it, "angle", 0) * 3.14159265f / 180, cs = std::cos(a), sn = std::sin(a);
                    for (int k = 0; k < 4; k++) {
                        float lx = (k == 1 || k == 2) ? hw : -hw, ly = k >= 2 ? hh : -hh;
                        ImVec2 hp(c.x + (lx * cs - ly * sn) * zoom, c.y + (lx * sn + ly * cs) * zoom);
                        if (std::fabs(io.MousePos.x - hp.x) <= 6 && std::fabs(io.MousePos.y - hp.y) <= 6) handle = k;
                    }
                    if (handle >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                    if (handle >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        drag = DScale; startSX = (float)jnum(*it, "scaleX", 1); startSY = (float)jnum(*it, "scaleY", 1);
                    }
                }
            }
            if (drag == DNone && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                std::string hit = pickAt(proj, sc, mw.x, mw.y);
                if (!hit.empty()) {
                    bool in = std::find(sel.begin(), sel.end(), hit) != sel.end();
                    if (io.KeyShift) { if (in) sel.erase(std::find(sel.begin(), sel.end(), hit)); else sel.push_back(hit); }
                    else if (!in) sel = {hit};
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && onOpenObject) {
                        if (json *it = findById(insts, hit)) onOpenObject(jstr(*it, "objectId"));
                    } else {
                        drag = DMove; dragStart = mw; startPos.clear();
                        for (auto &id : sel) if (json *it = findById(insts, id)) startPos[id] = ImVec2((float)jnum(*it, "x", 0), (float)jnum(*it, "y", 0));
                    }
                } else {
                    if (!io.KeyShift) sel.clear();
                    drag = DBox; boxA = mw;
                }
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                std::string hit = pickAt(proj, sc, mw.x, mw.y);
                if (!hit.empty()) { if (std::find(sel.begin(), sel.end(), hit) == sel.end()) sel = {hit}; ImGui::OpenPopup("##instctx"); }
            }
        } else if (mode == Place) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) placeAt(proj, sc, mw.x, mw.y);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                std::string hit = pickAt(proj, sc, mw.x, mw.y);
                if (!hit.empty()) { sel = {hit}; deleteSelected(sc); }
            }
        } else if (mode == Tiles && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            drag = DPaint;
        }
    }
    if (drag == DMove) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) drag = DNone;
        else {
            float dx = mw.x - dragStart.x, dy = mw.y - dragStart.y;
            bool sn = snap && !io.KeyAlt;
            for (auto &kv : startPos) if (json *it = findById(insts, kv.first)) {
                float k = hudK(sc, findById(proj.doc["objects"], jstr(*it, "objectId")));
                float nx = kv.second.x + dx / k, ny = kv.second.y + dy / k;
                (*it)["x"] = sn ? std::round(nx / gw) * gw : std::round(nx);
                (*it)["y"] = sn ? std::round(ny / gh) * gh : std::round(ny);
            }
        }
    } else if (drag == DScale) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || sel.size() != 1) drag = DNone;
        else if (json *it = findById(insts, sel[0])) {
            json *obj = findById(proj.doc["objects"], jstr(*it, "objectId"));
            ObjSize bs = obj ? objBaseSize(proj, *obj) : ObjSize{32, 32};
            float k = hudK(sc, obj);
            float a = -(float)jnum(*it, "angle", 0) * 3.14159265f / 180, dx = mw.x / k - (float)jnum(*it, "x", 0), dy = mw.y / k - (float)jnum(*it, "y", 0);
            float lx = std::fabs(dx * std::cos(a) - dy * std::sin(a)), ly = std::fabs(dx * std::sin(a) + dy * std::cos(a));
            float nsx = std::max(0.05f, lx * 2 / std::max(1.0f, bs.w)), nsy = std::max(0.05f, ly * 2 / std::max(1.0f, bs.h));
            if (io.KeyShift) { float k = std::max(nsx / std::fabs(startSX), nsy / std::fabs(startSY)); nsx = std::fabs(startSX) * k; nsy = std::fabs(startSY) * k; }
            (*it)["scaleX"] = std::round((startSX < 0 ? -nsx : nsx) * 100) / 100.0;
            (*it)["scaleY"] = std::round((startSY < 0 ? -nsy : nsy) * 100) / 100.0;
        }
    } else if (drag == DBox) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float x0 = std::min(boxA.x, mw.x), x1 = std::max(boxA.x, mw.x), y0 = std::min(boxA.y, mw.y), y1 = std::max(boxA.y, mw.y);
            if (x1 - x0 > 2 / zoom || y1 - y0 > 2 / zoom)
                for (auto &it : insts) {
                    json *obj = findById(proj.doc["objects"], jstr(it, "objectId"));
                    float k = hudK(sc, obj), x = k * (float)jnum(it, "x", 0), y = k * (float)jnum(it, "y", 0);
                    std::string id = jstr(it, "id");
                    if (x >= x0 && x <= x1 && y >= y0 && y <= y1 && obj && !layerHidden(proj, *obj) && std::find(sel.begin(), sel.end(), id) == sel.end()) sel.push_back(id);
                }
            drag = DNone;
        }
    } else if (drag == DPaint) {
        bool l = ImGui::IsMouseDown(ImGuiMouseButton_Left), r = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (!l && !r) drag = DNone;
        else paintCell(proj, sc, mw.x, mw.y, r ? -1 : paintTile);
    }
    if (ImGui::BeginPopup("##instctx")) {
        if (sel.size() == 1 && ImGui::MenuItem("Editar el objeto") && onOpenObject) if (json *it = findById(insts, sel[0])) onOpenObject(jstr(*it, "objectId"));
        if (ImGui::MenuItem("Duplicar", "Ctrl+D")) duplicateSelected(sc);
        if (ImGui::MenuItem("Traer al frente")) for (auto &id : sel) if (json *it = findById(insts, id)) (*it)["z"] = jnum(*it, "z", 0) + 1;
        if (ImGui::MenuItem("Enviar al fondo")) for (auto &id : sel) if (json *it = findById(insts, id)) (*it)["z"] = jnum(*it, "z", 0) - 1;
        ImGui::Separator();
        if (ImGui::MenuItem("Eliminar", "Supr")) deleteSelected(sc);
        ImGui::EndPopup();
    }

    /* ---- dibujo ---- */
    ImVec2 cend(canvasPos.x + avail.x, canvasPos.y + avail.y);
    dl->PushClipRect(canvasPos, cend, true);
    dl->AddRectFilled(canvasPos, cend, IM_COL32(18, 18, 23, 255));
    ImVec2 s0 = toScreen(0, 0), s1 = toScreen(SW, SH);
    Px bg = parseColor(jstr(sc, "background", "#202329"));
    dl->AddRectFilled(s0, s1, IM_COL32(bg.r, bg.g, bg.b, 255));
    if (grid && gw * zoom >= 6 && gh * zoom >= 6) {
        ImU32 gc = IM_COL32(255, 255, 255, 18);
        float x0 = std::max(0.0f, std::floor(toWorld(canvasPos).x / gw) * gw), x1 = std::min(SW, toWorld(cend).x);
        float y0 = std::max(0.0f, std::floor(toWorld(canvasPos).y / gh) * gh), y1 = std::min(SH, toWorld(cend).y);
        for (float x = x0; x <= x1; x += gw) dl->AddLine(toScreen(x, 0), toScreen(x, SH), gc);
        for (float y = y0; y <= y1; y += gh) dl->AddLine(toScreen(0, y), toScreen(SW, y), gc);
    }
    // por capas (de la mas lejana a la mas cercana): sus mapas de tiles y luego sus objetos, como en el juego
    auto layerIdOfTm = [&](const json &tm) { std::string l = jstr(tm, "layer"); return l.empty() || !findLayer(proj, l) ? std::string("base") : l; };
    auto drawTilemap = [&](json &tm) {
        json *ts = findById(proj.doc["tilesets"], jstr(tm, "tilesetId"));
        if (!ts || !tm.contains("data") || !tm["data"].is_array()) return;
        ImVec2 wa = toWorld(canvasPos), wb = toWorld(cend);
        float T = (float)jnum(*ts, "tileSize", 16), ox = (float)jnum(tm, "x", 0), oy = (float)jnum(tm, "y", 0);
        int cols = (int)jnum(tm, "cols", 0), rows = (int)jnum(tm, "rows", 0);
        int c0 = std::max(0, (int)std::floor((wa.x - ox) / T)), c1 = std::min(cols - 1, (int)std::floor((wb.x - ox) / T));
        int r0 = std::max(0, (int)std::floor((wa.y - oy) / T)), r1 = std::min(rows - 1, (int)std::floor((wb.y - oy) / T));
        const json &data = tm["data"];
        bool dim = mode == Tiles && jstr(tm, "id") != curTilemap;
        for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) {
            int idx = r * cols + c;
            if (idx >= (int)data.size() || !data[idx].is_number()) continue;
            int t = data[idx].get<int>();
            if (t < 0) continue;
            if (ui::PixTexture *tx = tileTex(proj, jstr(*ts, "id"), t))
                dl->AddImage(tx->id(), toScreen(ox + c * T, oy + r * T), toScreen(ox + (c + 1) * T, oy + (r + 1) * T), ImVec2(0, 0), ImVec2(1, 1), dim ? IM_COL32(255, 255, 255, 110) : IM_COL32_WHITE);
        }
        if (mode == Tiles && jstr(tm, "id") == curTilemap)
            dl->AddRect(toScreen(ox, oy), toScreen(ox + cols * T, oy + rows * T), IM_COL32(157, 125, 255, 200), 0, 0, 1.5f);
    };
    std::vector<std::pair<double, std::string>> lorder;
    if (proj.doc.contains("layers") && proj.doc["layers"].is_array())   // solo leer: projectLayers() crearia la lista
        for (auto &L : proj.doc["layers"]) lorder.push_back({jnum(L, "z", 0), jstr(L, "id")});
    if (!findLayer(proj, "base")) lorder.push_back({0, "base"});
    std::stable_sort(lorder.begin(), lorder.end(), [](auto &a, auto &b) { return a.first < b.first; });
    // instancias en el orden del juego: capa (z), z de la instancia y orden
    std::vector<std::pair<double, int>> order;
    for (int i = 0; i < (int)insts.size(); i++) {
        json *obj = findById(proj.doc["objects"], jstr(insts[i], "objectId"));
        const json *L = obj ? layerOf(proj, *obj) : nullptr;
        order.push_back({(L ? jnum(*L, "z", 0) : 0) * 1e6 + jnum(insts[i], "z", 0) * 1e3 + i * 1e-3, i});
    }
    std::sort(order.begin(), order.end());
    for (auto &lo : lorder) {
        const json *LL = findLayer(proj, lo.second);
        if (LL && jbool(*LL, "hidden", false)) continue;
        if (sc.contains("tilemaps") && sc["tilemaps"].is_array())
            for (auto &tm : sc["tilemaps"]) if (layerIdOfTm(tm) == lo.second) drawTilemap(tm);
        for (auto &oi : order) {
            json &it = insts[oi.second];
            json *obj = findById(proj.doc["objects"], jstr(it, "objectId"));
            if (!obj) continue;
            const json *L = layerOf(proj, *obj);
            if ((L ? jstr(*L, "id") : std::string("base")) != lo.second) continue;
            float k = hudK(sc, obj);
            drawObjectAt(dl, proj, *obj, toScreen(k * (float)jnum(it, "x", 0), k * (float)jnum(it, "y", 0)), (float)jnum(it, "scaleX", 1), (float)jnum(it, "scaleY", 1),
                         (float)jnum(it, "angle", 0), (float)jnum(it, "opacity", 100), zoom * k, (int)jnum(it, "frame", 0));
        }
    }
    dl->AddRect(s0, s1, IM_COL32(120, 120, 140, 255));
    // pantalla del juego (referencia de la interfaz y de la camara)
    ImVec2 v1 = toScreen(VW / camZoom(sc), VH / camZoom(sc));
    for (float x = s0.x; x < v1.x; x += 12) dl->AddLine(ImVec2(x, s0.y), ImVec2(std::min(x + 6, v1.x), s0.y), IM_COL32(255, 210, 120, 150), 1), dl->AddLine(ImVec2(x, v1.y), ImVec2(std::min(x + 6, v1.x), v1.y), IM_COL32(255, 210, 120, 150), 1);
    for (float y = s0.y; y < v1.y; y += 12) dl->AddLine(ImVec2(s0.x, y), ImVec2(s0.x, std::min(y + 6, v1.y)), IM_COL32(255, 210, 120, 150), 1), dl->AddLine(ImVec2(v1.x, y), ImVec2(v1.x, std::min(y + 6, v1.y)), IM_COL32(255, 210, 120, 150), 1);
    dl->AddText(ImVec2(v1.x - 64, v1.y + 3), IM_COL32(255, 210, 120, 180), "Pantalla");
    // seleccion (la propia y la de quienes trabajan en esta escena, con su color)
    auto outline = [&](const std::string &id, ImU32 col, bool handles) {
        json *it = findById(insts, id);
        json *obj = it ? findById(proj.doc["objects"], jstr(*it, "objectId")) : nullptr;
        if (!it || !obj) return;
        ObjSize bs = objBaseSize(proj, *obj);
        float k = hudK(sc, obj);
        float hw = k * bs.w * (float)jnum(*it, "scaleX", 1) / 2, hh = k * bs.h * (float)jnum(*it, "scaleY", 1) / 2;
        float cx = k * (float)jnum(*it, "x", 0), cy = k * (float)jnum(*it, "y", 0);
        if (jstr(*obj, "type") == "text") {
            std::string al = obj->contains("text") ? jstr((*obj)["text"], "align", "left") : "left";
            if (al == "left") cx += std::fabs(hw); else if (al == "right") cx -= std::fabs(hw);
        }
        ImVec2 c = toScreen(cx, cy);
        float a = (float)jnum(*it, "angle", 0) * 3.14159265f / 180, cs = std::cos(a), sn = std::sin(a);
        ImVec2 pts[4];
        for (int k = 0; k < 4; k++) {
            float lx = (k == 1 || k == 2) ? hw : -hw, ly = k >= 2 ? hh : -hh;
            pts[k] = ImVec2(c.x + (lx * cs - ly * sn) * zoom, c.y + (lx * sn + ly * cs) * zoom);
        }
        dl->AddPolyline(pts, 4, col, ImDrawFlags_Closed, 2.0f);
        if (handles && jstr(*obj, "type") == "sprite")
            for (auto &hp : pts) { dl->AddRectFilled(ImVec2(hp.x - 4, hp.y - 4), ImVec2(hp.x + 4, hp.y + 4), IM_COL32_WHITE); dl->AddRect(ImVec2(hp.x - 4, hp.y - 4), ImVec2(hp.x + 4, hp.y + 4), ui::kPrimary); }
    };
    const std::string myDoc = "scene:" + sceneId;
    for (auto &p : collab::peers())
        if (p.doc == myDoc) for (auto &id : p.sel) outline(id, p.color, false);
    for (auto &id : sel) outline(id, IM_COL32(157, 125, 255, 255), sel.size() == 1);
    if (drag == DBox) {
        ImVec2 a = toScreen(boxA.x, boxA.y);
        dl->AddRectFilled(a, io.MousePos, IM_COL32(157, 125, 255, 40));
        dl->AddRect(a, io.MousePos, IM_COL32(157, 125, 255, 200));
    }
    // vista previa al colocar / celda al pintar
    if (hovered && mode == Place && !placeObj.empty() && drag == DNone) {
        if (json *obj = findById(proj.doc["objects"], placeObj)) {
            float k = hudK(sc, obj);
            drawObjectAt(dl, proj, *obj, toScreen(k * snapV(mw.x / k, gw), k * snapV(mw.y / k, gh)), 1, 1, 0, 45, zoom * k, 0);
        }
    }
    if (hovered && mode == Tiles) {
        if (json *tm = tilemapById(sc, curTilemap)) {
            json *ts = findById(proj.doc["tilesets"], jstr(*tm, "tilesetId"));
            float T = ts ? (float)jnum(*ts, "tileSize", 16) : 16, ox = (float)jnum(*tm, "x", 0), oy = (float)jnum(*tm, "y", 0);
            float cx = ox + std::floor((mw.x - ox) / T) * T, cy = oy + std::floor((mw.y - oy) / T) * T;
            dl->AddRect(toScreen(cx, cy), toScreen(cx + T, cy + T), IM_COL32_WHITE, 0, 0, 1.5f);
        }
    }
    // cursores de los demas (flecha con su nombre) y el propio para ellos
    for (auto &p : collab::peers()) {
        if (p.doc != myDoc || !p.cursor) continue;
        ImVec2 c = toScreen(p.x, p.y);
        ImVec2 tri[3] = { c, ImVec2(c.x + 3, c.y + 17), ImVec2(c.x + 13, c.y + 12) };
        dl->AddTriangleFilled(tri[0], tri[1], tri[2], p.color);
        dl->AddTriangle(tri[0], tri[1], tri[2], IM_COL32(20, 20, 26, 255), 1.2f);
        ImVec2 ts = ImGui::CalcTextSize(p.name.c_str());
        ImVec2 lp(c.x + 12, c.y + 16);
        dl->AddRectFilled(lp, ImVec2(lp.x + ts.x + 10, lp.y + ts.y + 4), p.color, 4);
        dl->AddText(ImVec2(lp.x + 5, lp.y + 2), IM_COL32(20, 20, 26, 255), p.name.c_str());
    }
    if (collab::role() != collab::Role::None && (hovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
        collab::setPresence(myDoc, mw.x, mw.y, hovered, sel);
    dl->PopClipRect();
    if (hovered) {                                   // coordenadas del raton, solo con el raton sobre el lienzo
        char buf[96];
        snprintf(buf, sizeof buf, "%.0f, %.0f", mw.x, mw.y);
        dl->AddText(ImVec2(canvasPos.x + 8, cend.y - 22), IM_COL32(200, 200, 214, 255), buf);
    }
    if (mode == Tiles && !tilemapById(sc, curTilemap))
        dl->AddText(ImVec2(canvasPos.x + 8, canvasPos.y + 8), IM_COL32(255, 210, 120, 255), "Elige o crea un mapa de tiles en el panel de la derecha");
}

static void propRow(const char *l) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::SameLine(110); ImGui::SetNextItemWidth(-1); }

void SceneEditor::instanceProps(Project &proj, json &, json &it) {
    json *obj = findById(proj.doc["objects"], jstr(it, "objectId"));
    ImGui::PushFont(ui::boldFont(), 17.0f);
    ImGui::TextUnformatted(obj ? jstr(*obj, "name").c_str() : "?");
    ImGui::PopFont();
    if (obj && ImGui::Button("Editar el objeto", ImVec2(-1, 0)) && onOpenObject) onOpenObject(jstr(*obj, "id"));
    ImGui::SeparatorText("Instancia");
    propRow("X"); editNum("##x", it, "x", 0, 1);
    propRow("Y"); editNum("##y", it, "y", 0, 1);
    propRow("Z"); editNum("##z", it, "z", 0, 0.2f);
    if (obj) {
        ObjSize bs = objBaseSize(proj, *obj);
        double w = std::fabs(jnum(it, "scaleX", 1)) * bs.w, h = std::fabs(jnum(it, "scaleY", 1)) * bs.h;
        propRow("Ancho");
        if (ImGui::DragScalar("##w", ImGuiDataType_Double, &w, 1, nullptr, nullptr, "%.0f px") && bs.w > 0) it["scaleX"] = std::max(0.01, w / bs.w) * (jnum(it, "scaleX", 1) < 0 ? -1 : 1);
        propRow("Alto");
        if (ImGui::DragScalar("##h", ImGuiDataType_Double, &h, 1, nullptr, nullptr, "%.0f px") && bs.h > 0) it["scaleY"] = std::max(0.01, h / bs.h) * (jnum(it, "scaleY", 1) < 0 ? -1 : 1);
    }
    propRow("Rotación"); editNum("##a", it, "angle", 0, 1, "%.0f grados", -3600, 3600);
    propRow("Opacidad"); editNum("##o", it, "opacity", 100, 1, "%.0f %%", 0, 100);
    if (obj && jstr(*obj, "type", "sprite") == "sprite") { propRow("Frame"); editNum("##f", it, "frame", 0, 0.1f, "%.0f", 0, 999); }
    if (obj) {
        json layers = projectLayers(proj);
        if (jstr(*obj, "capa").empty()) (*obj)["capa"] = "base";
        propRow("Capa"); comboById("##capa", *obj, "capa", layers);
        ImGui::TextDisabled("La capa es del objeto (todas sus instancias).");
    }
    ImGui::SeparatorText("Variables de la instancia");
    if (!it.contains("vars") || !it["vars"].is_object()) it["vars"] = json::object();
    json &vars = it["vars"];
    std::string delKey;
    int k = 0;
    for (auto v = vars.begin(); v != vars.end(); ++v, ++k) {
        ImGui::PushID(k);
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(v.key().c_str()); ImGui::SameLine(110);
        ImGui::SetNextItemWidth(-60);
        std::string val = v.value().is_string() ? v.value().get<std::string>() : v.value().dump();
        char buf[256]; snprintf(buf, sizeof buf, "%s", val.c_str());
        if (ImGui::InputText("##v", buf, sizeof buf)) { char *e = nullptr; double d = strtod(buf, &e); v.value() = (buf[0] && e && *e == 0) ? json(d) : json(std::string(buf)); }
        ImGui::SameLine();
        if (ImGui::SmallButton("Quitar")) delKey = v.key();
        ImGui::PopID();
    }
    if (!delKey.empty()) vars.erase(delKey);
    static char nv[48] = "";
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##nv", "nombre de la variable", nv, sizeof nv);
    ImGui::SameLine();
    if (ImGui::Button("Añadir") && nv[0]) { vars[nv] = 0; nv[0] = 0; }
    ImGui::TextDisabled("Sustituyen el valor inicial de las variables del objeto solo en esta instancia.");
}

void SceneEditor::sceneProps(Project &proj, json &sc) {
    if (ImGui::CollapsingHeader("Escena", ImGuiTreeNodeFlags_DefaultOpen)) {
        propRow("Nombre"); editText("##sn", sc, "name");
        propRow("Ancho"); editNum("##sw", sc, "width", 960, 1, "%.0f px", 16, 100000);
        propRow("Alto"); editNum("##sh", sc, "height", 540, 1, "%.0f px", 16, 100000);
        propRow("Fondo"); editColor("##sbg", sc, "background", "#202329");
    }
    if (ImGui::CollapsingHeader("Luz")) {   // oscuridad de la escena; los objetos con el comportamiento Luz la iluminan
        json &lt = sc.contains("light") && sc["light"].is_object() ? sc["light"] : (sc["light"] = json::object());
        propRow("Oscuridad"); editNum("##ld", lt, "darkness", 0, 0.5f, "%.0f %%", 0, 100);
        propRow("Color"); editColor("##lc", lt, "color", "#05060f");
    }
    if (ImGui::CollapsingHeader("Cámara")) {
        json &cam = sc.contains("camera") && sc["camera"].is_object() ? sc["camera"] : (sc["camera"] = json::object());
        propRow("Zoom"); editNum("##cz", cam, "zoom", 1, 0.01f, "%.2f", 0.2, 4);
        propRow("Suavizado"); editNum("##cs", cam, "smooth", 0, 0.005f, "%.2f", 0, 1);
        propRow("Límites"); comboOptions("##cl", cam, "limits", {{"scene", "Toda la escena"}, {"custom", "Zona personalizada"}}, "scene");
        if (jstr(cam, "limits", "scene") == "custom") {
            propRow("X"); editNum("##lx", cam, "lx", 0); propRow("Y"); editNum("##ly", cam, "ly", 0);
            propRow("Ancho"); editNum("##lw", cam, "lw", jnum(sc, "width", 960)); propRow("Alto"); editNum("##lh", cam, "lh", jnum(sc, "height", 540));
        }
    }
    if (ImGui::CollapsingHeader("Variables de la escena")) {
        if (!sc.contains("vars") || !sc["vars"].is_array()) sc["vars"] = json::array();
        json &vars = sc["vars"];
        int del = -1;
        for (int i = 0; i < (int)vars.size(); i++) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(100); editText("##n", vars[i], "name");
            ImGui::SameLine(); ImGui::SetNextItemWidth(-60);
            std::string v = jstr(vars[i], "value");
            char buf[256]; snprintf(buf, sizeof buf, "%s", v.c_str());
            if (ImGui::InputText("##v", buf, sizeof buf)) { char *e = nullptr; double d = strtod(buf, &e); vars[i]["value"] = (buf[0] && e && *e == 0) ? json(d) : json(std::string(buf)); }
            ImGui::SameLine(); if (ImGui::SmallButton("Quitar")) del = i;
            ImGui::PopID();
        }
        if (del >= 0) vars.erase(vars.begin() + del);
        if (ImGui::Button("+ Variable")) vars.push_back({{"name", "var" + std::to_string(vars.size() + 1)}, {"value", 0}});
        ImGui::TextDisabled("Se cargan como variables al entrar en la escena.");
    }
    if (ImGui::CollapsingHeader("Efectos de la escena")) {
        if (!sc.contains("effects") || !sc["effects"].is_array()) sc["effects"] = json::array();
        json &fx = sc["effects"];
        static const std::vector<std::pair<std::string, std::string>> types = {{"filter", "Filtro de color"}, {"scanlines", "Líneas de escaneo"}, {"vignette", "Viñeta"},
            {"noise", "Ruido analógico"}, {"pixelate", "Pixelado"}, {"wave", "Onda"}, {"chroma", "Aberración cromática"}};
        int del = -1;
        for (int i = 0; i < (int)fx.size(); i++) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(140); comboOptions("##t", fx[i], "type", types, "filter");
            ImGui::SameLine(); ImGui::SetNextItemWidth(70); editNum("##a", fx[i], "amount", 50, 1, "%.0f", 0, 100);
            ImGui::SameLine(); if (ImGui::SmallButton("Quitar")) del = i;
            if (jstr(fx[i], "type", "filter") == "filter") { ImGui::SetNextItemWidth(-1); editText("##c", fx[i], "code", "saturate(1.2)"); }
            ImGui::PopID();
        }
        if (del >= 0) fx.erase(fx.begin() + del);
        if (ImGui::Button("+ Efecto")) fx.push_back({{"type", "vignette"}, {"amount", 50}});
        if (!proj.doc.contains("shaders")) proj.doc["shaders"] = json::array();
        propRow("Shader GLSL"); comboById("##shid", sc, "shaderId", proj.doc["shaders"], "(ninguno)");
        ImGui::TextDisabled("Código JavaScript de la escena (opcional, cada fotograma):");
        codeField("##scode", sc, "shaderCode", ImVec2(-1, 140), CodeLang::GLSL);
    }
}

void SceneEditor::tilemapProps(Project &proj, json &sc) {
    if (!ImGui::CollapsingHeader("Mapas de tiles", mode == Tiles ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    if (!sc.contains("tilemaps") || !sc["tilemaps"].is_array()) sc["tilemaps"] = json::array();
    json &tms = sc["tilemaps"];
    if (curTilemap.empty() && !tms.empty()) curTilemap = jstr(tms[0], "id");
    for (auto &tm : tms) {
        std::string id = jstr(tm, "id");
        ImGui::PushID(id.c_str());
        if (ImGui::Selectable(jstr(tm, "name", "Mapa").c_str(), id == curTilemap)) { curTilemap = id; mode = Tiles; }
        ImGui::PopID();
    }
    bool haveTs = proj.doc.contains("tilesets") && !proj.doc["tilesets"].empty();
    ImGui::BeginDisabled(!haveTs);
    if (ImGui::Button("+ Mapa de tiles")) { addTilemap(proj, sc); mode = Tiles; }
    ImGui::EndDisabled();
    if (!haveTs) ImGui::TextDisabled("No hay conjuntos de tiles en el proyecto.");
    json *tm = tilemapById(sc, curTilemap);
    if (!tm) return;
    ImGui::SameLine();
    if (ui::deleteButton("Eliminar mapa", "el mapa de tiles \"" + jstr(*tm, "name", "sin nombre") + "\"")) {
        for (size_t i = 0; i < tms.size(); i++) if (jstr(tms[i], "id") == curTilemap) { tms.erase(tms.begin() + i); break; }
        curTilemap.clear();
        return;
    }
    propRow("Nombre"); editText("##tn", *tm, "name");
    propRow("Conjunto"); comboById("##tts", *tm, "tilesetId", proj.doc["tilesets"]);
    if (jstr(*tm, "layer").empty()) (*tm)["layer"] = "base";
    propRow("Capa"); comboById("##tly", *tm, "layer", projectLayers(proj));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Capa del mapa: su parallax, su shader y si sus tiles chocan (capa sólida)");
    propRow("Origen X"); editNum("##tx", *tm, "x", 0);
    propRow("Origen Y"); editNum("##ty", *tm, "y", 0);
    int cols = (int)jnum(*tm, "cols", 1), rows = (int)jnum(*tm, "rows", 1), nc = cols, nr = rows;
    propRow("Columnas"); ImGui::InputInt("##tc", &nc);
    propRow("Filas"); ImGui::InputInt("##tr", &nr);
    nc = std::clamp(nc, 1, 2000); nr = std::clamp(nr, 1, 2000);
    if (nc != cols || nr != rows) {   // redimensionar conservando lo pintado
        json old = (*tm)["data"], nd = json::array();
        for (int r = 0; r < nr; r++) for (int c = 0; c < nc; c++) {
            int oi = r * cols + c;
            nd.push_back(r < rows && c < cols && old.is_array() && oi < (int)old.size() ? old[oi] : json(-1));
        }
        (*tm)["data"] = nd; (*tm)["cols"] = nc; (*tm)["rows"] = nr;
    }
    json *ts = findById(proj.doc["tilesets"], jstr(*tm, "tilesetId"));
    if (!ts || !ts->contains("tiles")) return;
    ImGui::SeparatorText("Tile para pintar");
    int n = (int)(*ts)["tiles"].size();
    ImGui::Checkbox("Autotile (bordes solos)", &g_autotile);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Usa 16 tiles seguidos desde el elegido: el primero sin vecinos y los demás según los\n"
                                                  "vecinos iguales (arriba 1 + derecha 2 + abajo 4 + izquierda 8).");
    g_autoBase = paintTile >= 0 && paintTile + 16 <= n ? paintTile : -1;
    if (g_autotile && g_autoBase < 0) ImGui::TextDisabled("Elige un tile que tenga 15 más después.");
    float cell = 30, avail = ImGui::GetContentRegionAvail().x;
    int per = std::max(1, (int)(avail / (cell + 4)));
    for (int i = 0; i < n; i++) {
        ImGui::PushID(i);
        if (i % per) ImGui::SameLine(0, 4);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##t", ImVec2(cell, cell))) { paintTile = i; mode = Tiles; }
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ui::checkerboard(dl, p, ImVec2(p.x + cell, p.y + cell), 5);
        if (ui::PixTexture *tx = tileTex(proj, jstr(*ts, "id"), i)) dl->AddImage(tx->id(), p, ImVec2(p.x + cell, p.y + cell));
        const json &tl = (*ts)["tiles"][i];
        if (jbool(tl, "solid", false)) dl->AddRectFilled(ImVec2(p.x + cell - 7, p.y + cell - 7), ImVec2(p.x + cell - 1, p.y + cell - 1), IM_COL32(240, 96, 93, 255));
        if (i == paintTile) dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + cell + 2, p.y + cell + 2), IM_COL32_WHITE, 0.0f, 0, 2.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tile %d%s", i, jbool(tl, "solid", false) ? " (sólido)" : "");
        ImGui::PopID();
    }
}

void SceneEditor::layersProps(Project &proj) {
    if (!ImGui::CollapsingHeader("Capas")) return;
    json &layers = projectLayers(proj);
    int del = -1;
    std::vector<int> idx(layers.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = (int)i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return jnum(layers[a], "z", 0) > jnum(layers[b], "z", 0); });
    for (int i : idx) {
        json &L = layers[i];
        ImGui::PushID(i);
        bool hidden = jbool(L, "hidden", false);
        if (ui::iconButton("eye", hidden ? Icon::EyeOff : Icon::Eye, false, "Mostrar u ocultar en el editor y en el juego", 24)) L["hidden"] = !hidden;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110); editText("##ln", L, "name");
        ImGui::SameLine(); ImGui::SetNextItemWidth(46); editNum("##lz", L, "z", 0, 0.1f, "z %.0f");
        ImGui::SameLine(); ImGui::SetNextItemWidth(56); editNum("##lp", L, "parallax", 1, 0.01f, "%.2f", 0, 4);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paralaje: 1 se mueve con la cámara, 0.5 a la mitad (fondos)");
        if (!proj.doc.contains("shaders")) proj.doc["shaders"] = json::array();
        ImGui::SameLine(); ImGui::SetNextItemWidth(90); comboById("##lsh", L, "shaderId", proj.doc["shaders"], "(sin shader)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shader de la capa (por ejemplo, Lluvia)");
        if (!jstr(L, "shaderId").empty()) {
            ImGui::SameLine(); editBool("##lsc", L, "shaderContentOnly", false);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shader solo donde la capa tiene algo: mucho más rápido (vegetación, agua...).\nNo sirve para shaders que pintan por toda la pantalla, como la lluvia");
        }
        { bool solid = jbool(L, "solid", true); ImGui::SameLine(); if (ImGui::Checkbox("##lsol", &solid)) L["solid"] = solid; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sólida: sus mapas de tiles y sus objetos sólidos chocan. Desmárcala en capas de decorado");
        if (jstr(L, "id") != "base") { ImGui::SameLine(); if (ImGui::SmallButton("x")) del = i; }
        ImGui::PopID();
    }
    if (del >= 0) {
        std::string id = jstr(layers[del], "id");
        layers.erase(layers.begin() + del);
        for (auto &o : proj.doc["objects"]) if (jstr(o, "capa") == id) o["capa"] = "base";
    }
    if (ImGui::Button("+ Capa")) {
        double z = 0;
        for (auto &L : layers) z = std::max(z, jnum(L, "z", 0));
        layers.push_back({{"id", makeUid("ly")}, {"name", "Capa " + std::to_string(layers.size() + 1)}, {"z", z + 1}, {"parallax", 1}});
    }
}

void SceneEditor::drawProps(Project &proj, json &sc) {
    if (sel.size() == 1) {
        if (json *it = findById(sc["instances"], sel[0])) { instanceProps(proj, sc, *it); return; }
    }
    if (sel.size() > 1) {
        ImGui::Text("%d instancias seleccionadas", (int)sel.size());
        if (ImGui::Button("Duplicar", ImVec2(-1, 0))) duplicateSelected(sc);
        if (ImGui::Button("Eliminar", ImVec2(-1, 0))) deleteSelected(sc);
        ImGui::TextDisabled("Arrastralas para moverlas juntas.");
        return;
    }
    sceneProps(proj, sc);
    tilemapProps(proj, sc);
    layersProps(proj);
}

void SceneEditor::shortcuts(Project &, json &sc) {
    ui::noteFocus("scene", sceneId);
    if (int h = ui::takeHistoryRequest()) { if (h > 0) hist.doRedo(sc); else hist.doUndo(sc); sel.clear(); }
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
    ImGuiIO &io = ImGui::GetIO();
    float gw = (float)jnum(sc, "gridW", 16), gh = (float)jnum(sc, "gridH", 16);
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) hist.doRedo(sc); else hist.doUndo(sc); sel.clear(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) { hist.doRedo(sc); sel.clear(); }
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected(sc);
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) { sel.clear(); for (auto &it : sc["instances"]) sel.push_back(jstr(it, "id")); }
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) { clip.clear(); for (auto &id : sel) if (json *it = findById(sc["instances"], id)) clip.push_back(*it); }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false) && !clip.empty()) {
            sel.clear();
            for (auto c : clip) { c["id"] = makeUid("i"); c["x"] = jnum(c, "x", 0) + gw; c["y"] = jnum(c, "y", 0) + gh; sel.push_back(jstr(c, "id")); sc["instances"].push_back(c); }
        }
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) deleteSelected(sc);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { sel.clear(); mode = Select; }
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) mode = Select;
    if (ImGui::IsKeyPressed(ImGuiKey_P, false)) mode = Place;
    if (ImGui::IsKeyPressed(ImGuiKey_T, false)) mode = Tiles;
    float step = io.KeyShift ? 1.0f : 0.0f;
    float sx = step ? gw : 1, sy = step ? gh : 1;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) moveSelected(sc, -sx, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) moveSelected(sc, sx, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) moveSelected(sc, 0, -sy);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) moveSelected(sc, 0, sy);
}

void SceneEditor::draw(Project &proj) {
    json *scp = findById(proj.doc["scenes"], sceneId);
    if (!scp) { open = false; return; }
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title(proj).c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); hist.track(*scp); return; }
    json &sc = *scp;
    if (!sc.contains("instances") || !sc["instances"].is_array()) sc["instances"] = json::array();
    json before = sc;
    json layersBefore = proj.doc.contains("layers") ? proj.doc["layers"] : json();
    // seleccion valida
    sel.erase(std::remove_if(sel.begin(), sel.end(), [&](const std::string &id) { return !findById(sc["instances"], id); }), sel.end());
    shortcuts(proj, sc);
    drawToolbar(proj, sc);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float left = 200, right = 300;
    ImGui::BeginChild("##objs", ImVec2(left, avail.y), ImGuiChildFlags_None);
    drawObjectsPanel(proj, sc);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##canvas", ImVec2(avail.x - left - right - 16, avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas(proj, sc);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##props", ImVec2(right, avail.y), ImGuiChildFlags_None);
    drawProps(proj, sc);
    ImGui::EndChild();
    ImGui::End();
    if (sc != before || (proj.doc.contains("layers") ? proj.doc["layers"] : json()) != layersBefore) proj.dirty = true;
    hist.track(sc);
}
