#include "tileset_editor.hpp"
#include "collision_editor.hpp"
#include <algorithm>

std::string createTileset(Project &proj, const std::string &name, int T, int cols, int rows) {
    if (!proj.doc.contains("tilesets") || !proj.doc["tilesets"].is_array()) proj.doc["tilesets"] = json::array();
    json tiles = json::array();
    for (int i = 0; i < cols * rows; i++) {
        json px = json::array();
        for (int k = 0; k < T * T; k++) px.push_back("");
        tiles.push_back({{"pixels", px}, {"solid", false}});
    }
    std::string id = makeUid("ts");
    proj.doc["tilesets"].push_back({{"id", id}, {"name", name}, {"tileSize", T}, {"columns", cols}, {"rows", rows}, {"tiles", tiles}});
    proj.dirty = true; proj.rev++;
    return id;
}

std::string TilesetEditor::title(Project &proj) {
    json *ts = findById(proj.doc["tilesets"], tilesetId);
    return "Tiles: " + (ts ? jstr(*ts, "name") : std::string("?")) + "###ts_" + tilesetId;
}

void TilesetEditor::reload(Project &proj) {
    json *ts = findById(proj.doc["tilesets"], tilesetId);
    if (!ts) return;
    int T = (int)jnum(*ts, "tileSize", 16);
    json sp = {{"id", tilesetId}, {"name", jstr(*ts, "name")}, {"width", T}, {"height", T}, {"frames", json::array()}};
    if (ts->contains("tiles")) for (auto &t : (*ts)["tiles"]) sp["frames"].push_back({{"pixels", t.contains("pixels") ? t["pixels"] : json::array()}});
    int keepFrame = pix.curFrame;
    pix.load(sp);
    pix.tileMode = true;
    pix.curFrame = std::clamp(keepFrame, 0, std::max(0, (int)pix.st.layers[0].cels.size() - 1));
    loaded = true;
}

void TilesetEditor::writeBack(Project &proj) {
    json *ts = findById(proj.doc["tilesets"], tilesetId);
    if (!ts) return;
    if (!ts->contains("tiles") || !(*ts)["tiles"].is_array()) (*ts)["tiles"] = json::array();
    json &tiles = (*ts)["tiles"];
    std::vector<Px> comp;
    int n = (int)pix.st.layers[0].cels.size();
    for (int f = 0; f < n; f++) {
        pix.composite(f, comp, false);
        json px = json::array();
        for (auto &c : comp) px.push_back(colorToString(c));
        if (f >= (int)tiles.size()) tiles.push_back({{"solid", false}});
        tiles[f]["pixels"] = px;
    }
    (*ts)["tileSize"] = pix.st.w;
    proj.dirty = true; proj.rev++;
}

void TilesetEditor::setGrid(Project &proj, int T, int cols, int rows) {
    json *ts = findById(proj.doc["tilesets"], tilesetId);
    if (!ts) return;
    T = std::clamp(T, 4, 128); cols = std::clamp(cols, 1, 64); rows = std::clamp(rows, 1, 64);
    writeBack(proj);
    int oldT = (int)jnum(*ts, "tileSize", 16);
    json &tiles = (*ts)["tiles"];
    for (auto &t : tiles) {
        if (oldT == T) break;
        json np = json::array();
        for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
            int oi = y * oldT + x;
            np.push_back(x < oldT && y < oldT && t.contains("pixels") && oi < (int)t["pixels"].size() ? t["pixels"][oi] : json(""));
        }
        t["pixels"] = np;
    }
    int want = cols * rows;
    while ((int)tiles.size() < want) {
        json px = json::array();
        for (int k = 0; k < T * T; k++) px.push_back("");
        tiles.push_back({{"pixels", px}, {"solid", false}});
    }
    while ((int)tiles.size() > want) tiles.erase(tiles.end() - 1);
    (*ts)["tileSize"] = T; (*ts)["columns"] = cols; (*ts)["rows"] = rows;
    proj.dirty = true; proj.rev++;
    reload(proj);
}

void TilesetEditor::draw(Project &proj) {
    json *ts = findById(proj.doc["tilesets"], tilesetId);
    if (!ts) { open = false; return; }
    if (!loaded) reload(proj);
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title(proj).c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##tsleft", ImVec2(270, avail.y));
    ImGui::SeparatorText("Conjunto");
    ImGui::SetNextItemWidth(-1);
    if (editText("##tsname", *ts, "name")) proj.dirty = true;
    int T = (int)jnum(*ts, "tileSize", 16), cols = (int)jnum(*ts, "columns", 4), rows = (int)jnum(*ts, "rows", 2);
    int nT = T, nc = cols, nr = rows;
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Tamaño"); ImGui::SameLine(90); ImGui::SetNextItemWidth(-1);
    const char *sizes[] = {"8 px", "16 px", "24 px", "32 px", "48 px", "64 px"}; int sv[] = {8, 16, 24, 32, 48, 64};
    int si = 1; for (int k = 0; k < 6; k++) if (sv[k] == T) si = k;
    if (ImGui::Combo("##tsz", &si, sizes, 6)) nT = sv[si];
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Columnas"); ImGui::SameLine(90); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##tc", &nc);
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Filas"); ImGui::SameLine(90); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##tr", &nr);
    if (nT != T || nc != cols || nr != rows) setGrid(proj, nT, nc, nr);
    ImGui::SeparatorText("Tiles");
    json &tiles = (*ts)["tiles"];
    float cell = std::min(36.0f, (ImGui::GetContentRegionAvail().x - 4) / std::max(1, cols) - 3);
    for (int i = 0; i < (int)tiles.size(); i++) {
        ImGui::PushID(i);
        if (i % std::max(1, cols)) ImGui::SameLine(0, 3);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##t", ImVec2(cell, cell))) pix.curFrame = i;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ui::checkerboard(dl, p, ImVec2(p.x + cell, p.y + cell), 5);
        if (ui::PixTexture *tx = tileTex(proj, tilesetId, i)) dl->AddImage(tx->id(), p, ImVec2(p.x + cell, p.y + cell));
        const json &t = tiles[i];
        float d = 6;
        if (jbool(t, "solid", false) && !jbool(t, "oneway", false)) dl->AddRectFilled(ImVec2(p.x + cell - d, p.y + cell - d), ImVec2(p.x + cell, p.y + cell), IM_COL32(240, 96, 93, 255));
        if (jbool(t, "oneway", false)) dl->AddRectFilled(ImVec2(p.x + cell - d, p.y + cell - d), ImVec2(p.x + cell, p.y + cell), IM_COL32(59, 130, 246, 255));
        if (jbool(t, "ladder", false)) dl->AddRectFilled(ImVec2(p.x, p.y + cell - d), ImVec2(p.x + d, p.y + cell), IM_COL32(34, 197, 94, 255));
        if (jbool(t, "danger", false)) dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + d, p.y + d), IM_COL32(250, 204, 21, 255));
        if (i == pix.curFrame) dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + cell + 2, p.y + cell + 2), IM_COL32_WHITE, 0.0f, 0, 2.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tile %d", i);
        ImGui::PopID();
    }
    int cur = std::clamp(pix.curFrame, 0, (int)tiles.size() - 1);
    if (!tiles.empty()) {
        ImGui::SeparatorText(("Tile " + std::to_string(cur)).c_str());
        json &t = tiles[cur];
        bool solid = jbool(t, "solid", false) && !jbool(t, "oneway", false), oneway = jbool(t, "oneway", false), ladder = jbool(t, "ladder", false);
        if (ImGui::Checkbox("Sólido (los personajes chocan)", &solid)) { t["solid"] = solid; if (solid) t["oneway"] = false; proj.dirty = true; }
        if (ImGui::Checkbox("Traspasable (se atraviesa desde abajo)", &oneway)) { t["oneway"] = oneway; t["solid"] = oneway; proj.dirty = true; }
        if (ImGui::Checkbox("Escalera", &ladder)) { t["ladder"] = ladder; proj.dirty = true; }
        bool danger = jbool(t, "danger", false);
        if (ImGui::Checkbox("Peligro (pinchos)", &danger)) { t["danger"] = danger; proj.dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("En los eventos se pregunta con la condición \"toca un tile peligroso\"");
        if (jbool(t, "solid", false) || jbool(t, "oneway", false) || jbool(t, "ladder", false) || jbool(t, "danger", false)) {
            ImGui::SeparatorText("Colisión del tile");
            if (!t.contains("collision")) t["collision"] = nullptr;
            float mw = std::min(220.0f, ImGui::GetContentRegionAvail().x - 60);
            if (collisionEditor("##tilecol", t["collision"], tileTex(proj, tilesetId, cur), T, T, mw)) proj.dirty = true;
            ImGui::TextDisabled("Medio tile = escalón; con Puntos, una rampa. El personaje los sube andando.");
        }
        ImGui::Spacing();
        if (ImGui::Button("Copiar al siguiente") && cur + 1 < (int)tiles.size()) {
            pix.pushUndo();
            for (auto &L : pix.st.layers) L.cels[cur + 1] = L.cels[cur];
            pix.curFrame = cur + 1; pix.dirtyForProject = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Limpiar tile")) {
            pix.pushUndo();
            for (auto &L : pix.st.layers) std::fill(L.cels[cur].begin(), L.cels[cur].end(), Px{});
            pix.dirtyForProject = true;
        }
        ImGui::TextDisabled("Rojo: sólido  Azul: traspasable  Verde: escalera");
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##tspix", ImVec2(0, avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    pix.drawBody(proj);
    ImGui::EndChild();
    ImGui::End();
    if (pix.dirtyForProject) { writeBack(proj); pix.dirtyForProject = false; }
}
