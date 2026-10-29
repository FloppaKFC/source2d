#include "layers_autotiles.hpp"
#include "scene_editor.hpp"
#include "tiles_obj.hpp"
#include <algorithm>
#include <cmath>

using ui::Icon;

static void row(const char *l, float w = 150) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::SameLine(w); ImGui::SetNextItemWidth(-1); }

/* ---------------- capas ---------------- */
// Cada capa es una tarjeta con todas sus opciones; la lista se recorre con la rueda (de la mas cercana a la mas lejana,
// como las capas de Source Sprite)
void LayersEditor::draw(Project &proj) {
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(800, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Capas###layers", &open)) { ImGui::End(); return; }
    ui::noteFocus("layers");
    json &layers = projectLayers(proj);
    json before = layers;
    if (ui::primaryButton("+ Nueva capa")) {
        double z = 0;
        for (auto &L : layers) z = std::max(z, jnum(L, "z", 0));
        layers.push_back({{"id", makeUid("ly")}, {"name", "Capa " + std::to_string(layers.size() + 1)}, {"z", z + 1}, {"parallax", 1}});
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Arriba las que se dibujan encima. Las capas son de todo el proyecto (todas las escenas).");
    ImGui::BeginChild("##layerlist", ImVec2(0, 0), ImGuiChildFlags_None);
    std::vector<int> idx(layers.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = (int)i;
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return jnum(layers[a], "z", 0) > jnum(layers[b], "z", 0); });
    int del = -1, moveUp = -1, moveDown = -1;
    if (!proj.doc.contains("shaders")) proj.doc["shaders"] = json::array();
    for (size_t k = 0; k < idx.size(); k++) {
        int i = idx[k];
        json &L = layers[i];
        ImGui::PushID(jstr(L, "id").c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kPanel);
        ImGui::BeginChild("##card", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        bool hidden = jbool(L, "hidden", false);
        if (ui::iconButton("eye", hidden ? Icon::EyeOff : Icon::Eye, false, "Mostrar u ocultar en el editor y en el juego", 26)) L["hidden"] = !hidden;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        ImGui::PushFont(ui::boldFont(), 16.0f);
        editText("##ln", L, "name");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::BeginDisabled(k == 0);
        if (ui::iconButton("up", Icon::Up, false, "Subir (se dibuja encima)", 26)) moveUp = (int)k;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(k + 1 == idx.size());
        if (ui::iconButton("down", Icon::Down, false, "Bajar", 26)) moveDown = (int)k;
        ImGui::EndDisabled();
        if (jstr(L, "id") != "base") {
            ImGui::SameLine();
            if (ui::deleteButton("Eliminar", "la capa \"" + jstr(L, "name") + "\" (sus objetos pasan a la Principal)")) del = i;
        } else { ImGui::SameLine(); ImGui::TextDisabled("(capa principal)"); }
        row("Orden (z)"); editNum("##lz", L, "z", 0, 0.1f, "%.0f");
        row("Paralaje"); editNum("##lp", L, "parallax", 1, 0.01f, "%.2f", 0, 4);
        ImGui::TextDisabled("1 = se mueve con la cámara; 0.5 = a la mitad (fondos lejanos); 0 = quieta.");
        bool solid = jbool(L, "solid", true);
        ImGui::Dummy(ImVec2(146, 0)); ImGui::SameLine();
        if (ImGui::Checkbox("Sólida (sus tiles y sus objetos sólidos chocan)", &solid)) L["solid"] = solid;
        row("Shader"); comboById("##lsh", L, "shaderId", proj.doc["shaders"], "(sin shader)");
        if (!jstr(L, "shaderId").empty()) {
            ImGui::Dummy(ImVec2(146, 0)); ImGui::SameLine();
            editBool("Shader solo donde la capa tiene algo (más rápido; no para lluvia)", L, "shaderContentOnly", false);
        }
        ImGui::SeparatorText("Oscuridad");
        row("Oscuridad"); editNum("##ld", L, "darkness", 0, 0.5f, "%.0f %%", 0, 100);
        if (jnum(L, "darkness", 0) > 0) { row("Color"); editColor("##ldc", L, "darkColor", "#05060f"); }
        ImGui::TextDisabled("Oscurece esta capa y las de debajo; las luces (objeto Luz) la iluminan. Las capas de encima\n"
                            "(por ejemplo, la interfaz) siguen con luz.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::EndChild();
    // subir o bajar: intercambia el z con la vecina (si tienen el mismo, se separan)
    auto swapZ = [&](int a, int b) {
        double za = jnum(layers[a], "z", 0), zb = jnum(layers[b], "z", 0);
        if (za == zb) zb = za + (b == idx[0] ? 1 : -1);
        layers[a]["z"] = zb; layers[b]["z"] = za;
    };
    if (moveUp > 0) swapZ(idx[moveUp], idx[moveUp - 1]);
    if (moveDown >= 0 && moveDown + 1 < (int)idx.size()) swapZ(idx[moveDown], idx[moveDown + 1]);
    if (del >= 0) {
        std::string id = jstr(layers[del], "id");
        layers.erase(layers.begin() + del);
        for (auto &o : proj.doc["objects"]) if (jstr(o, "capa") == id) o["capa"] = "base";
    }
    if (layers != before) proj.dirty = true;
    ImGui::End();
}

/* ---------------- autotiles ---------------- */
std::string createAutotile(Project &proj) {
    json &ats = projectAutotiles(proj);
    std::string id = makeUid("at"), ts;
    if (proj.doc.contains("tilesets") && proj.doc["tilesets"].is_array() && !proj.doc["tilesets"].empty()) ts = jstr(proj.doc["tilesets"][0], "id");
    json tiles = json::array();
    int n = 0;
    if (!ts.empty()) if (json *t = findById(proj.doc["tilesets"], ts)) n = t->contains("tiles") ? (int)(*t)["tiles"].size() : 0;
    for (int m = 0; m < 16; m++) tiles.push_back(n >= 16 ? m : (n > 0 ? 0 : -1));   // con 16 tiles: en orden
    ats.push_back({{"id", id}, {"name", "Autotile " + std::to_string(ats.size() + 1)}, {"tilesetId", ts}, {"tiles", tiles}});
    proj.dirty = true;
    return id;
}

// dibujo de una forma: el centro y los brazos hacia los vecinos que tiene (arriba 1, derecha 2, abajo 4, izquierda 8)
static void drawMask(ImDrawList *dl, ImVec2 p, float s, int m, ImU32 col) {
    float u = s / 3;
    dl->AddRectFilled(ImVec2(p.x + u, p.y + u), ImVec2(p.x + 2 * u, p.y + 2 * u), col);
    if (m & 1) dl->AddRectFilled(ImVec2(p.x + u, p.y), ImVec2(p.x + 2 * u, p.y + u), col);
    if (m & 2) dl->AddRectFilled(ImVec2(p.x + 2 * u, p.y + u), ImVec2(p.x + s, p.y + 2 * u), col);
    if (m & 4) dl->AddRectFilled(ImVec2(p.x + u, p.y + 2 * u), ImVec2(p.x + 2 * u, p.y + s), col);
    if (m & 8) dl->AddRectFilled(ImVec2(p.x, p.y + u), ImVec2(p.x + u, p.y + 2 * u), col);
}

void AutotilesEditor::draw(Project &proj) {
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(900, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Autotiles###autotiles", &open)) { ImGui::End(); return; }
    ui::noteFocus("autotiles");
    json &ats = projectAutotiles(proj);
    json before = ats;
    // lista
    ImGui::BeginChild("##atlist", ImVec2(220, 0), ImGuiChildFlags_None);
    if (ui::primaryButton("+ Nuevo autotile", ImVec2(-1, 0))) { cur = createAutotile(proj); test.clear(); }
    int del = -1;
    for (size_t i = 0; i < ats.size(); i++) {
        ImGui::PushID((int)i);
        if (ImGui::Selectable(jstr(ats[i], "name", "Autotile").c_str(), jstr(ats[i], "id") == cur)) { cur = jstr(ats[i], "id"); test.clear(); }
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (ImGui::MenuItem("Duplicar")) { json c = ats[i]; c["id"] = makeUid("at"); c["name"] = jstr(c, "name") + " copia"; ats.insert(ats.begin() + i + 1, c); }
            if (ImGui::MenuItem("Eliminar")) del = (int)i;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (ats.empty()) ImGui::TextWrapped("Un autotile pone solo los bordes y las esquinas: pintas y cada celda elige su tile según sus vecinas.");
    ImGui::EndChild();
    if (del >= 0) {
        std::string id = jstr(ats[del], "id");
        ats.erase(ats.begin() + del);
        for (auto &o : proj.doc["objects"]) if (jstr(o, "autotileId") == id) o.erase("autotileId");
        if (cur == id) cur.clear();
    }
    ImGui::SameLine();
    ImGui::BeginChild("##atedit", ImVec2(0, 0), ImGuiChildFlags_None);
    json *at = findById(ats, cur);
    if (!at && !ats.empty()) { cur = jstr(ats[0], "id"); at = &ats[0]; }
    if (!at) { ImGui::TextDisabled("Crea un autotile con el botón de la izquierda."); ImGui::EndChild(); ImGui::End(); return; }
    if (!at->contains("tiles") || !(*at)["tiles"].is_array()) (*at)["tiles"] = json::array();
    json &tiles = (*at)["tiles"];
    while (tiles.size() < 16) tiles.push_back(-1);
    if (!proj.doc.contains("tilesets")) proj.doc["tilesets"] = json::array();
    row("Nombre", 120); editText("##an", *at, "name");
    row("Mapa de tiles", 120); comboById("##ats", *at, "tilesetId", proj.doc["tilesets"]);
    json *ts = findById(proj.doc["tilesets"], jstr(*at, "tilesetId"));
    int nt = ts && ts->contains("tiles") ? (int)(*ts)["tiles"].size() : 0;
    std::string tsId = ts ? jstr(*ts, "id") : "";
    float T = ts ? (float)jnum(*ts, "tileSize", 16) : 16;

    ImGui::SeparatorText("1. Elige una forma y luego su tile");
    float cs = 44;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (int m = 0; m < 16; m++) {
        ImGui::PushID(m);
        if (m % 8) ImGui::SameLine(0, 6);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##slot", ImVec2(cs, cs + 18))) slot = m;
        dl->AddRectFilled(p, ImVec2(p.x + cs, p.y + cs), IM_COL32(22, 22, 28, 255), 4);
        int t = tiles[m].is_number() ? tiles[m].get<int>() : -1;
        if (ui::PixTexture *tx = t >= 0 ? tileTex(proj, tsId, t) : nullptr) dl->AddImage(tx->id(), ImVec2(p.x + 4, p.y + 4), ImVec2(p.x + cs - 4, p.y + cs - 4));
        drawMask(dl, ImVec2(p.x + cs / 2 - 8, p.y + cs + 2), 15, m, m == slot ? ui::kPrimary : IM_COL32(150, 150, 165, 255));
        if (m == slot) dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + cs + 2, p.y + cs + 2), IM_COL32_WHITE, 4.0f, 0, 2.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Celda con vecinas:%s%s%s%s%s", m == 0 ? " ninguna" : "", m & 1 ? " arriba" : "", m & 2 ? " derecha" : "", m & 4 ? " abajo" : "", m & 8 ? " izquierda" : "");
        ImGui::PopID();
    }
    if (nt >= 16 && ImGui::Button("Usar 16 tiles seguidos desde el de la forma elegida")) {
        int base = tiles[slot].is_number() ? std::max(0, tiles[slot].get<int>()) : 0;
        if (base + 16 > nt) base = nt - 16;
        for (int m = 0; m < 16; m++) tiles[m] = base + m;
    }
    if (nt > 0) {
        ImGui::TextDisabled("Tiles de \"%s\" (clic: lo asigna a la forma elegida y pasa a la siguiente):", jstr(*ts, "name").c_str());
        float cell = 32, avail = ImGui::GetContentRegionAvail().x;
        int per = std::max(1, (int)(avail / (cell + 4)));
        for (int i = 0; i < nt; i++) {
            ImGui::PushID(1000 + i);
            if (i % per) ImGui::SameLine(0, 4);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##t", ImVec2(cell, cell))) { tiles[slot] = i; slot = (slot + 1) % 16; }
            ui::checkerboard(dl, p, ImVec2(p.x + cell, p.y + cell), 5);
            if (ui::PixTexture *tx = tileTex(proj, tsId, i)) dl->AddImage(tx->id(), p, ImVec2(p.x + cell, p.y + cell));
            if (tiles[slot].is_number() && tiles[slot].get<int>() == i) dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + cell + 2, p.y + cell + 2), IM_COL32_WHITE, 0.0f, 0, 2.0f);
            ImGui::PopID();
        }
    } else ImGui::TextDisabled("Elige un mapa de tiles que tenga tiles.");

    ImGui::SeparatorText("2. Pruébalo");
    const int TC = 16, TR = 8;
    if ((int)test.size() != TC * TR) test.assign(TC * TR, 0);
    float z = std::max(1.0f, std::floor(std::min((ImGui::GetContentRegionAvail().x - 4) / (TC * T), 220.0f / (TR * T))));
    float sz = T * z;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##test", ImVec2(TC * sz, TR * sz), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        int c = (int)((mp.x - p0.x) / sz), r = (int)((mp.y - p0.y) / sz);
        if (c >= 0 && r >= 0 && c < TC && r < TR) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) test[r * TC + c] = 1;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) test[r * TC + c] = 0;
        }
    }
    dl->AddRectFilled(p0, ImVec2(p0.x + TC * sz, p0.y + TR * sz), IM_COL32(32, 35, 41, 255));
    auto in = [&](int c, int r) { return c >= 0 && r >= 0 && c < TC && r < TR && test[r * TC + c]; };
    for (int r = 0; r < TR; r++) for (int c = 0; c < TC; c++) {
        ImVec2 a(p0.x + c * sz, p0.y + r * sz), b(a.x + sz, a.y + sz);
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 14));
        if (!in(c, r)) continue;
        int m = (in(c, r - 1) ? 1 : 0) | (in(c + 1, r) ? 2 : 0) | (in(c, r + 1) ? 4 : 0) | (in(c - 1, r) ? 8 : 0);
        int t = autotilePick(*at, m);
        if (ui::PixTexture *tx = t >= 0 ? tileTex(proj, tsId, t) : nullptr) dl->AddImage(tx->id(), a, b);
        else dl->AddRectFilled(a, b, IM_COL32(157, 125, 255, 120));
    }
    if (ImGui::Button("Borrar la prueba")) std::fill(test.begin(), test.end(), 0);
    ImGui::SameLine();
    ImGui::TextDisabled("Izquierdo pinta, derecho borra. Para usarlo: objeto Tile > Activar autotile.");
    ImGui::EndChild();
    if (ats != before) proj.dirty = true;
    ImGui::End();
}
