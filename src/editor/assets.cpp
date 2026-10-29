#include "assets.hpp"
#include "fonts.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

double jnum(const json &o, const char *key, double def) {
    if (!o.is_object() || !o.contains(key)) return def;
    const json &v = o[key];
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) { char *e = nullptr; std::string s = v.get<std::string>(); double d = strtod(s.c_str(), &e); return e && e != s.c_str() ? d : def; }
    if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    return def;
}
std::string jstr(const json &o, const char *key, const std::string &def) {
    if (!o.is_object() || !o.contains(key)) return def;
    const json &v = o[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) { char b[32]; snprintf(b, sizeof b, "%g", v.get<double>()); return b; }
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return def;
}
bool jbool(const json &o, const char *key, bool def) {
    if (!o.is_object() || !o.contains(key)) return def;
    const json &v = o[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0;
    if (v.is_string()) return v.get<std::string>() == "true";
    return def;
}
json *findById(json &arr, const std::string &id) {
    if (!arr.is_array()) return nullptr;
    for (auto &x : arr) if (x.is_object() && x.value("id", "") == id) return &x;
    return nullptr;
}

/* ---------------- texturas ---------------- */
struct CachedTex { ui::PixTexture tex; int rev = -1; };
static std::map<std::string, CachedTex> g_tex;

static ui::PixTexture *cached(const std::string &key, int rev, int w, int h, const json *pixels) {
    CachedTex &c = g_tex[key];
    if (c.rev != rev || !c.tex.tex.id) {
        std::vector<Px> px(std::max(1, w * h));
        if (pixels && pixels->is_array())
            for (int i = 0; i < w * h && i < (int)pixels->size(); i++) if ((*pixels)[i].is_string()) px[i] = parseColor((*pixels)[i].get<std::string>());
        c.tex.update(px, std::max(1, w), std::max(1, h));
        c.rev = rev;
    }
    return &c.tex;
}

ui::PixTexture *spriteFrameTex(Project &proj, const std::string &spriteId, int frame) {
    json *sp = proj.spriteById(spriteId);
    if (!sp) return nullptr;
    int nf = sp->contains("frames") && (*sp)["frames"].is_array() ? (int)(*sp)["frames"].size() : 0;
    if (nf <= 0) return nullptr;
    frame = ((frame % nf) + nf) % nf;
    const json &f = (*sp)["frames"][frame];
    return cached("s:" + spriteId + ":" + std::to_string(frame), proj.rev, (int)jnum(*sp, "width", 16), (int)jnum(*sp, "height", 16),
                  f.contains("pixels") ? &f["pixels"] : nullptr);
}

ui::PixTexture *tileTex(Project &proj, const std::string &tilesetId, int tile) {
    json *ts = findById(proj.doc["tilesets"], tilesetId);
    if (!ts || tile < 0 || !ts->contains("tiles") || tile >= (int)(*ts)["tiles"].size()) return nullptr;
    int T = (int)jnum(*ts, "tileSize", 16);
    const json &t = (*ts)["tiles"][tile];
    return cached("t:" + tilesetId + ":" + std::to_string(tile), proj.rev, T, T, t.contains("pixels") ? &t["pixels"] : nullptr);
}

void releaseTextures() {
    for (auto &kv : g_tex) kv.second.tex.release();
    g_tex.clear();
}

/* ---------------- objetos y eventos por escena ---------------- */
bool objectInScene(const json &obj, const std::string &sceneId) {
    std::string s = jstr(obj, "scene");
    return sceneId.empty() || s.empty() || s == sceneId;
}

json sceneObjects(Project &proj, const std::string &sceneId) {
    json out = json::array();
    if (proj.doc.contains("objects") && proj.doc["objects"].is_array())
        for (auto &o : proj.doc["objects"]) if (objectInScene(o, sceneId)) out.push_back(o);
    return out;
}

json &sceneEvents(Project &proj, const std::string &sceneId) {
    if (!sceneId.empty() && proj.doc.contains("scenes"))
        if (json *sc = findById(proj.doc["scenes"], sceneId)) {
            if (!sc->contains("events") || !(*sc)["events"].is_array()) (*sc)["events"] = json::array();
            return (*sc)["events"];
        }
    if (!proj.doc.contains("events") || !proj.doc["events"].is_array()) proj.doc["events"] = json::array();
    return proj.doc["events"];
}

std::string sceneName(Project &proj, const std::string &sceneId) {
    json *sc = proj.doc.contains("scenes") ? findById(proj.doc["scenes"], sceneId) : nullptr;
    return sc ? jstr(*sc, "name") : std::string();
}

/* ---------------- objetos ---------------- */
const char *objTypeLabel(const std::string &type) {
    return type == "text" ? "Texto" : type == "button" ? "Botón" : type == "tile" ? "Tile" : type == "group" ? "Grupo" : type == "light" ? "Luz" : "Sprite";
}

ObjSize objBaseSize(Project &proj, const json &obj) {
    std::string type = jstr(obj, "type", "sprite");
    if (type == "text") {
        const json t = obj.contains("text") ? obj["text"] : json::object();
        float size = (float)jnum(t, "size", 24);
        std::string content = jstr(t, "content", "Texto");
        if (proj.doc.contains("fonts") && !jstr(t, "font").empty())
            if (json *f = findById(proj.doc["fonts"], jstr(t, "font")); f && jstr(*f, "kind") == "pixel")
                return {std::max(8.0f, pixelTextWidth(*f, content) * size / (float)std::max(1.0, jnum(*f, "cellH", 12))), size};
        return {std::max(8.0f, content.size() * size * 0.56f), size * 1.35f};
    }
    if (type == "button") {
        const json b = obj.contains("button") ? obj["button"] : json::object();
        return {(float)jnum(b, "width", 160), (float)jnum(b, "height", 42)};
    }
    if (json *sp = proj.spriteById(jstr(obj, "spriteId"))) return {(float)jnum(*sp, "width", 32), (float)jnum(*sp, "height", 32)};
    return {32, 32};
}

static ImU32 colorU32(const std::string &s, int alpha) {
    Px p = parseColor(s);
    return IM_COL32(p.r, p.g, p.b, p.a * alpha / 255);
}

void drawObjectAt(ImDrawList *dl, Project &proj, const json &obj, ImVec2 c, float sx, float sy, float ang, float opacity, float zoom, int frame) {
    int alpha = (int)(std::clamp(opacity, 0.0f, 100.0f) * 2.55f);
    std::string type = jstr(obj, "type", "sprite");
    // contorno (outline): la imagen repetida en 8 direcciones con el color del contorno
    static const float kOlDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, kOlDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    float ow = obj.contains("outline") && obj["outline"].is_object() ? (float)jnum(obj["outline"], "width", 0) : 0;
    ImU32 olCol = ow > 0 ? colorU32(jstr(obj["outline"], "color", "#000000"), alpha) : 0;
    float rad = ang * 3.14159265f / 180.0f, cs = std::cos(rad), sn = std::sin(rad);
    auto corner = [&](float lx, float ly) { return ImVec2(c.x + (lx * cs - ly * sn) * zoom, c.y + (lx * sn + ly * cs) * zoom); };
    ObjSize bs = objBaseSize(proj, obj);
    float hw = bs.w * sx / 2, hh = bs.h * sy / 2;
    if (type == "text") {
        const json t = obj.contains("text") ? obj["text"] : json::object();
        float size = (float)jnum(t, "size", 24) * zoom * std::fabs(sy);
        std::string content = jstr(t, "content", "Texto"), align = jstr(t, "align", "left");
        if (!jstr(t, "font").empty()) {   // fuente del proyecto: se dibuja como imagen
            float fsc = 1;
            if (ui::PixTexture *tx = textTexture(proj, jstr(t, "font"), content, (float)jnum(t, "size", 24), jstr(t, "color", "#ffffff"), jbool(t, "bold", true), &fsc)) {
                float w = tx->w * fsc * zoom * std::fabs(sx), h = tx->h * fsc * zoom * std::fabs(sy);
                float x = align == "center" ? c.x - w / 2 : align == "right" ? c.x - w : c.x;
                if (ow > 0) for (int k = 0; k < 8; k++) {
                    float dx = kOlDx[k] * ow * zoom, dy = kOlDy[k] * ow * zoom;
                    dl->AddImage(tx->id(), ImVec2(x + dx, c.y - h / 2 + dy), ImVec2(x + w + dx, c.y + h / 2 + dy), ImVec2(0, 0), ImVec2(1, 1), olCol);
                }
                dl->AddImage(tx->id(), ImVec2(x, c.y - h / 2), ImVec2(x + w, c.y + h / 2), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
            }
            return;
        }
        ImFont *f = ui::gameFont();
        ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0, content.c_str());
        float x = align == "center" ? c.x - ts.x / 2 : align == "right" ? c.x - ts.x : c.x;
        dl->AddText(f, size, ImVec2(x, c.y - ts.y / 2), colorU32(jstr(t, "color", "#ffffff"), alpha), content.c_str());
        return;
    }
    if (type == "button") {
        const json b = obj.contains("button") ? obj["button"] : json::object();
        ImVec2 a = corner(-hw, -hh), e = corner(hw, hh);
        dl->AddRectFilled(ImVec2(std::min(a.x, e.x), std::min(a.y, e.y)), ImVec2(std::max(a.x, e.x), std::max(a.y, e.y)),
                          colorU32(jstr(b, "bg", "#4a76c4"), alpha), (float)jnum(b, "radius", 6) * zoom);
        std::string txt = jstr(b, "text", "Boton");
        if (!jstr(b, "font").empty()) {
            float fsc = 1;
            if (ui::PixTexture *tx = textTexture(proj, jstr(b, "font"), txt, (float)jnum(b, "fontSize", 16), jstr(b, "fg", "#ffffff"), jbool(b, "bold", true), &fsc)) {
                float w = tx->w * fsc * zoom, h = tx->h * fsc * zoom;
                dl->AddImage(tx->id(), ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
            }
            return;
        }
        float fs = (float)jnum(b, "fontSize", 16) * zoom;
        ImVec2 ts = ui::gameFont()->CalcTextSizeA(fs, FLT_MAX, 0, txt.c_str());
        dl->AddText(ui::gameFont(), fs, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), colorU32(jstr(b, "fg", "#ffffff"), alpha), txt.c_str());
        return;
    }
    if (type == "light") {   // luz: su radio como un halo y una bombilla en el centro
        const json L = obj.contains("light") ? obj["light"] : json::object();
        Px lc = parseColor(jstr(L, "color", "#ffd9a0"));
        float r = (float)jnum(L, "radius", 160) * zoom;
        for (int k = 6; k >= 1; k--) dl->AddCircleFilled(c, r * k / 6, IM_COL32(lc.r, lc.g, lc.b, alpha * 10 / 255), 48);
        dl->AddCircle(c, r, IM_COL32(lc.r, lc.g, lc.b, alpha * 90 / 255), 48, 1);
        dl->AddCircleFilled(c, 7, IM_COL32(lc.r, lc.g, lc.b, alpha), 16);
        dl->AddCircle(c, 7, IM_COL32(30, 30, 36, alpha), 16, 1.5f);
        return;
    }
    std::string spId = jstr(obj, "spriteId");
    ui::PixTexture *tx = spriteFrameTex(proj, spId, frame);
    if (!tx) {
        // objeto sin sprite: un recuadro con su nombre (por ejemplo, objetos de tipo tile)
        ImVec2 a = corner(-16, -16), e = corner(16, 16);
        dl->AddRect(a, e, IM_COL32(157, 125, 255, alpha), 0, 0, 1.5f);
        dl->AddText(ImVec2(a.x + 3, a.y + 2), IM_COL32(200, 190, 255, alpha), jstr(obj, "name").c_str());
        return;
    }
    if (ow > 0) {
        // silueta del color del contorno: la textura con el color como tinte (en blanco y negro sirve tal cual)
        for (int k = 0; k < 8; k++) {
            ImVec2 d(kOlDx[k] * ow * zoom, kOlDy[k] * ow * zoom);
            auto sh = [&](ImVec2 v) { return ImVec2(v.x + d.x, v.y + d.y); };
            dl->AddImageQuad(tx->id(), sh(corner(-hw, -hh)), sh(corner(hw, -hh)), sh(corner(hw, hh)), sh(corner(-hw, hh)),
                             ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), olCol);
        }
    }
    dl->AddImageQuad(tx->id(), corner(-hw, -hh), corner(hw, -hh), corner(hw, hh), corner(-hw, hh),
                     ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), IM_COL32(255, 255, 255, alpha));
}

void objectThumb(Project &proj, const json &obj, float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    objectThumbAt(ImGui::GetWindowDrawList(), proj, obj, p, size);
}

void objectThumbAt(ImDrawList *dl, Project &proj, const json &obj, ImVec2 p, float size) {
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), IM_COL32(22, 22, 28, 255), 4);
    std::string type = jstr(obj, "type", "sprite");
    if (type == "tile" && proj.doc.contains("tilesets"))   // su primer tile
        if (ui::PixTexture *tt = tileTex(proj, jstr(obj, "tilesetId"), 0)) { dl->AddImage(tt->id(), ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + size - 3, p.y + size - 3)); return; }
    if (type == "light") {
        Px lc = parseColor(obj.contains("light") ? jstr(obj["light"], "color", "#ffd9a0") : "#ffd9a0");
        ImVec2 c(p.x + size / 2, p.y + size / 2);
        for (int k = 4; k >= 1; k--) dl->AddCircleFilled(c, size * 0.45f * k / 4, IM_COL32(lc.r, lc.g, lc.b, 40), 24);
        dl->AddCircleFilled(c, size * 0.16f, IM_COL32(lc.r, lc.g, lc.b, 255), 16);
        return;
    }
    if (type == "text" || type == "button" || type == "tile" || type == "group") {
        const char *l = type == "text" ? "T" : type == "button" ? "B" : type == "group" ? "G" : "#";
        ImVec2 ts = ImGui::CalcTextSize(l);
        dl->AddText(ImVec2(p.x + (size - ts.x) / 2, p.y + (size - ts.y) / 2), ui::kAccent, l);
        return;
    }
    ui::PixTexture *tx = spriteFrameTex(proj, jstr(obj, "spriteId"), 0);
    if (!tx || !tx->w) return;
    float sc = (size - 4) / std::max(tx->w, tx->h);
    float w = tx->w * sc, h = tx->h * sc;
    dl->AddImage(tx->id(), ImVec2(p.x + (size - w) / 2, p.y + (size - h) / 2), ImVec2(p.x + (size + w) / 2, p.y + (size + h) / 2));
}

/* ---------------- campos ligados al JSON ---------------- */
bool editNum(const char *label, json &o, const char *key, double def, float speed, const char *fmt, double mn, double mx) {
    double v = jnum(o, key, def);
    bool ch = ImGui::DragScalar(label, ImGuiDataType_Double, &v, speed, mn < mx ? &mn : nullptr, mn < mx ? &mx : nullptr, fmt);
    if (ch) { if (mn < mx) v = std::clamp(v, mn, mx); o[key] = (std::floor(v) == v && std::fabs(v) < 1e12) ? json((long long)v) : json(v); }
    return ch;
}
bool editInt(const char *label, json &o, const char *key, int def, int mn, int mx) {
    int v = (int)jnum(o, key, def);
    bool ch = ImGui::InputInt(label, &v);
    if (ch) { if (mn < mx) v = std::clamp(v, mn, mx); o[key] = v; }
    return ch;
}
bool editText(const char *label, json &o, const char *key, const char *hint) {
    std::string s = jstr(o, key);
    char buf[512]; snprintf(buf, sizeof buf, "%s", s.c_str());
    bool ch = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof buf) : ImGui::InputText(label, buf, sizeof buf);
    if (ch) o[key] = std::string(buf);
    return ch;
}
bool editMultiline(const char *label, json &o, const char *key, float height) {
    std::string s = jstr(o, key);
    std::vector<char> buf(std::max<size_t>(4096, s.size() + 2048));
    memcpy(buf.data(), s.c_str(), s.size() + 1);
    bool ch = ImGui::InputTextMultiline(label, buf.data(), buf.size(), ImVec2(-1, height), ImGuiInputTextFlags_AllowTabInput);
    if (ch) o[key] = std::string(buf.data());
    return ch;
}
bool editBool(const char *label, json &o, const char *key, bool def) {
    bool v = jbool(o, key, def);
    bool ch = ImGui::Checkbox(label, &v);
    if (ch) o[key] = v;
    return ch;
}
bool editColor(const char *label, json &o, const char *key, const char *def) {
    Px p = parseColor(jstr(o, key, def));
    float c[4] = {p.r / 255.f, p.g / 255.f, p.b / 255.f, p.a / 255.f};
    bool ch = ImGui::ColorEdit4(label, c, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex);
    if (ch) {
        Px n{(uint8_t)(c[0] * 255 + .5f), (uint8_t)(c[1] * 255 + .5f), (uint8_t)(c[2] * 255 + .5f), (uint8_t)(c[3] * 255 + .5f)};
        std::string s = colorToString(n);
        o[key] = s.empty() ? "#00000000" : s;
    }
    return ch;
}
bool comboById(const char *label, json &o, const char *key, json &list, const char *noneLabel) {
    std::string cur = jstr(o, key), curName = noneLabel ? noneLabel : "(ninguno)";
    if (json *x = findById(list, cur)) curName = jstr(*x, "name", cur);
    bool ch = false;
    if (ImGui::BeginCombo(label, curName.c_str())) {
        if (noneLabel && ImGui::Selectable(noneLabel, cur.empty())) { o[key] = ""; ch = true; }
        for (auto &x : list) {
            std::string id = jstr(x, "id");
            ImGui::PushID(id.c_str());
            if (ImGui::Selectable(jstr(x, "name", id).c_str(), id == cur)) { o[key] = id; ch = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return ch;
}
bool comboOptions(const char *label, json &o, const char *key, const std::vector<std::pair<std::string, std::string>> &opts, const std::string &def) {
    std::string cur = jstr(o, key, def), curName = cur;
    for (auto &op : opts) if (op.first == cur) curName = op.second;
    bool ch = false;
    if (ImGui::BeginCombo(label, curName.c_str())) {
        for (auto &op : opts) if (ImGui::Selectable(op.second.c_str(), op.first == cur)) { o[key] = op.first; ch = true; }
        ImGui::EndCombo();
    }
    return ch;
}

/* ---------------- historial ---------------- */
void JsonHistory::track(const json &cur) {
    if (!init) { stable = cur; init = true; return; }
    if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) return;
    if (cur != stable) {
        undo.push_back(stable);
        if (undo.size() > 100) undo.erase(undo.begin());
        redo.clear();
        stable = cur;
    }
}
bool JsonHistory::doUndo(json &cur) {
    if (undo.empty()) return false;
    redo.push_back(cur); cur = undo.back(); undo.pop_back(); stable = cur;
    return true;
}
bool JsonHistory::doRedo(json &cur) {
    if (redo.empty()) return false;
    undo.push_back(cur); cur = redo.back(); redo.pop_back(); stable = cur;
    return true;
}
