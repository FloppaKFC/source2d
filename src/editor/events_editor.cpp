#include "events_editor.hpp"
#include "school.hpp"
#include "collab.hpp"
#include "code_editor.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include "imgui_internal.h"
#include "instructions_json.h"
#include "object_editor.hpp"

using ui::Icon;

// escena de la hoja que se esta dibujando (las listas de objetos de los parametros salen de ella)
static std::string g_sheetScene;

/* ---------------- definiciones ---------------- */
InstrDefs &InstrDefs::get() {
    static InstrDefs g;
    if (g.d.is_null()) g.d = json::parse(std::string((const char *)instructions_json, instructions_json_len));
    return g;
}
const json *InstrDefs::def(const std::string &kind, const std::string &type) const {
    const char *k = kind == "cond" ? "cond" : "act";
    const json &m = d[k];
    auto it = m.find(type);
    if (it != m.end()) return &*it;
    auto ie = ext[k].find(type);
    return ie == ext[k].end() ? nullptr : &*ie;
}
std::vector<std::pair<std::string, const json *>> InstrDefs::all(const std::string &kind) const {
    const char *k = kind == "cond" ? "cond" : "act";
    std::vector<std::pair<std::string, const json *>> out;
    for (auto it = d[k].begin(); it != d[k].end(); ++it) out.push_back({it.key(), &*it});
    for (auto it = ext[k].begin(); it != ext[k].end(); ++it) out.push_back({it.key(), &*it});
    return out;
}
std::string InstrDefs::gdCat(const json &df) const {
    if (df.contains("gd")) return jstr(df, "gd");
    std::string cat = jstr(df, "cat", "General");
    if (d["oldCatGd"].contains(cat)) return d["oldCatGd"][cat].get<std::string>();
    if (cat.rfind("Comportamiento", 0) == 0) return "Mecánica de juego";
    return "Otros";
}

static const ImU32 kTxt = IM_COL32(235, 235, 242, 255), kObj = IM_COL32(127, 208, 255, 255), kVal = IM_COL32(255, 210, 122, 255),
                   kStr = IM_COL32(155, 226, 155, 255), kOp = IM_COL32(255, 159, 209, 255), kBad = IM_COL32(255, 143, 140, 255);

static ImU32 catColor(const std::string &c) {
    if (c == "Eventos y control") return IM_COL32(213, 194, 106, 255);
    if (c == "Entrada") return IM_COL32(92, 184, 230, 255);
    if (c == "Audio") return IM_COL32(230, 127, 181, 255);
    if (c == "Cámara") return IM_COL32(127, 209, 168, 255);
    if (c == "Mecánica de juego") return IM_COL32(157, 125, 255, 255);
    if (c == "Efecto visual") return IM_COL32(240, 163, 94, 255);
    if (c == "General") return IM_COL32(168, 168, 188, 255);
    return IM_COL32(111, 199, 192, 255);
}

// icono de cada condicion o accion (se elige por el tipo y, si no, por la categoria), dibujado con lineas
enum class IG { Key, Mouse, Pad, Note, Camera, Clock, Eye, Move, Rotate, Scale, Var, List, File, Code, Func, Loop, Flag,
                Always, Plus, Trash, Heart, Spark, Text, Film, Scene, Hit, Grid, Jump, Fall, Button, Dice, Mail, Pause,
                Wait, Curve, Shake, Save, Color, Speed, Tag };
static IG instrGlyph(const std::string &t, const std::string &cat) {
    auto has = [&](const char *w) { return t.find(w) != std::string::npos; };
    if (cat == "Archivos") return IG::File;
    if (t == "always") return IG::Always;
    if (t == "atStart") return IG::Flag;
    if (t == "wait") return IG::Wait;
    if (has("pause") || t == "gamePaused") return IG::Pause;
    if (has("essage")) return IG::Mail;
    if (t == "random" || has("rand")) return IG::Dice;
    if (has("pad") || t == "vibrate") return IG::Pad;
    if (has("key") || has("Key") || t == "anyKeyDown" || t == "anyKeyPressed") return IG::Key;
    if (has("mouse") || has("wheel") || t == "clicked" || has("Cursor") || t == "cursorTile") return IG::Mouse;
    if (has("ound") || has("usic") || t == "sfx" || t == "beep" || has("hannel")) return IG::Note;
    if (has("camera") || t == "shakeCamera") return t == "shakeCamera" ? IG::Shake : IG::Camera;
    if (has("imer") || t == "every" || t == "timeFromStart" || t == "timeScale") return IG::Clock;
    if (has("ween")) return IG::Curve;
    if (t == "create") return IG::Plus;
    if (t == "removeObj") return IG::Trash;
    if (has("ealth") || t == "damage" || t == "heal" || t == "isDead") return IG::Heart;
    if (t == "particles" || has("hader") || has("Effect") || t == "setOutline" || t == "flash" || t == "isFlashing") return IG::Spark;
    if (has("ext") || t == "typewriter") return IG::Text;
    if (has("nim") || has("Frame")) return IG::Film;
    if (has("cene") || t == "endGame" || t == "quitGame" || t == "stopGame" || t == "fullscreen") return IG::Scene;
    if (t == "collision" || t == "separate" || t == "distance" || t == "standsOn" || t == "pointInside") return IG::Hit;
    if (has("ile") || cat == "Tile") return IG::Grid;
    if (has("ump") || t == "platJumping") return IG::Jump;
    if (t == "platFalling" || has("ravity") || t == "objGrounded" || t == "onGround") return IG::Fall;
    if (has("btn") || has("utton")) return IG::Button;
    if (has("ave") || has("oad")) return IG::Save;
    if (t == "jsCode") return IG::Code;
    if (has("unction") || t == "returnValue") return IG::Func;
    if (t == "breakLoop") return IG::Loop;
    if (has("rray") || has("vars") || has("Json")) return IG::List;
    if (has("Var") || has("var") || has("Global") || has("Bool") || t == "cmpNum" || t == "cmpStr" || t == "exprTrue") return IG::Var;
    if (has("ngle") || has("otate") || has("Spin") || t == "lookAt") return IG::Rotate;
    if (has("cale")) return IG::Scale;
    if (has("pacity") || t == "show" || t == "hide" || has("isible") || t == "objOutside") return IG::Eye;
    if (has("olor") || t == "setBgColor") return IG::Color;
    if (has("peed") || has("V") || t == "addForce" || t == "setFriction" || t == "setBounce") return IG::Speed;
    if (has("ove") || has("Pos") || has("ToX") || has("ToY") || t == "setX" || t == "setY" || t == "setZ" || t == "objPos" || t == "objInRect") return IG::Move;
    if (cat == "Archivos") return IG::File;
    if (cat == "Variables") return IG::Var;
    if (cat == "Control" || cat == "Funciones") return IG::Func;
    if (cat == "Apariencia") return IG::Eye;
    if (cat == "Movimiento" || cat == "Física") return IG::Move;
    return IG::Tag;
}
static void drawInstrIcon(ImDrawList *dl, const std::string &type, const std::string &cat, ImVec2 c, float s, ImU32 col) {
    float h = s * 0.5f, t = std::max(1.3f, s / 11.0f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
    auto txt = [&](const char *str, float sz) {
        ImFont *f = ImGui::GetFont();
        ImVec2 ts = f->CalcTextSizeA(sz, FLT_MAX, 0, str);
        dl->AddText(f, sz, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), col, str);
    };
    switch (instrGlyph(type, cat)) {
    case IG::Key: dl->AddRect(P(-0.9f, -0.55f), P(0.9f, 0.55f), col, 2, 0, t);
        for (int i = 0; i < 3; i++) dl->AddRectFilled(P(-0.6f + i * 0.45f, -0.25f), P(-0.35f + i * 0.45f, -0.05f), col);
        dl->AddLine(P(-0.45f, 0.25f), P(0.45f, 0.25f), col, t); break;
    case IG::Mouse: dl->AddRect(P(-0.5f, -0.8f), P(0.5f, 0.8f), col, h * 0.45f, 0, t); dl->AddLine(P(0, -0.8f), P(0, -0.2f), col, t); break;
    case IG::Pad: ui::drawIcon(dl, Icon::Gamepad, c, s, col); break;
    case IG::Note: dl->AddCircleFilled(P(-0.35f, 0.55f), h * 0.28f, col); dl->AddLine(P(-0.1f, 0.55f), P(-0.1f, -0.8f), col, t);
        dl->AddLine(P(-0.1f, -0.8f), P(0.6f, -0.45f), col, t); break;
    case IG::Camera: dl->AddRect(P(-0.9f, -0.45f), P(0.9f, 0.7f), col, 2, 0, t); dl->AddCircle(P(0, 0.12f), h * 0.32f, col, 12, t);
        dl->AddRectFilled(P(-0.35f, -0.7f), P(0.25f, -0.45f), col); break;
    case IG::Clock: dl->AddCircle(c, h * 0.85f, col, 16, t); dl->AddLine(c, P(0, -0.55f), col, t); dl->AddLine(c, P(0.4f, 0.15f), col, t); break;
    case IG::Eye: dl->AddBezierQuadratic(P(-0.95f, 0), P(0, -0.9f), P(0.95f, 0), col, t); dl->AddBezierQuadratic(P(-0.95f, 0), P(0, 0.9f), P(0.95f, 0), col, t);
        dl->AddCircleFilled(c, h * 0.25f, col); break;
    case IG::Move: ui::drawIcon(dl, Icon::Move, c, s, col); break;
    case IG::Rotate: dl->PathArcTo(c, h * 0.75f, -2.6f, 1.9f, 14); dl->PathStroke(col, 0, t);
        dl->AddTriangleFilled(P(0.55f, -0.75f), P(0.95f, -0.45f), P(0.35f, -0.25f), col); break;
    case IG::Scale: dl->AddRect(P(-0.8f, -0.1f), P(0.1f, 0.8f), col, 0, 0, t); dl->AddLine(P(-0.1f, 0.1f), P(0.8f, -0.8f), col, t);
        dl->AddLine(P(0.8f, -0.8f), P(0.3f, -0.8f), col, t); dl->AddLine(P(0.8f, -0.8f), P(0.8f, -0.3f), col, t); break;
    case IG::Var: txt("x=", s * 0.95f); break;
    case IG::List: txt("[ ]", s * 0.9f); break;
    case IG::File: dl->AddQuad(P(-0.6f, -0.85f), P(0.3f, -0.85f), P(0.6f, 0.85f), P(-0.6f, 0.85f), col, t);
        dl->AddLine(P(-0.3f, -0.2f), P(0.3f, -0.2f), col, t); dl->AddLine(P(-0.3f, 0.25f), P(0.35f, 0.25f), col, t); break;
    case IG::Code: txt("</>", s * 0.8f); break;
    case IG::Func: txt("f()", s * 0.85f); break;
    case IG::Loop: dl->PathArcTo(c, h * 0.7f, 0.3f, 5.8f, 16); dl->PathStroke(col, 0, t); dl->AddTriangleFilled(P(0.45f, -0.1f), P(0.95f, -0.1f), P(0.7f, 0.35f), col); break;
    case IG::Flag: dl->AddLine(P(-0.6f, -0.9f), P(-0.6f, 0.9f), col, t); dl->AddTriangleFilled(P(-0.6f, -0.9f), P(0.8f, -0.45f), P(-0.6f, 0.0f), col); break;
    case IG::Always: dl->PathArcTo(P(-0.45f, 0), h * 0.4f, 0, 6.283f, 14); dl->PathStroke(col, 0, t);
        dl->PathArcTo(P(0.45f, 0), h * 0.4f, 0, 6.283f, 14); dl->PathStroke(col, 0, t); break;
    case IG::Plus: ui::drawIcon(dl, Icon::Plus, c, s, col); break;
    case IG::Trash: ui::drawIcon(dl, Icon::Trash, c, s, col); break;
    case IG::Heart: dl->AddCircleFilled(P(-0.38f, -0.25f), h * 0.42f, col); dl->AddCircleFilled(P(0.38f, -0.25f), h * 0.42f, col);
        dl->AddTriangleFilled(P(-0.8f, -0.05f), P(0.8f, -0.05f), P(0, 0.85f), col); break;
    case IG::Spark: dl->AddQuadFilled(P(0, -0.95f), P(0.22f, -0.22f), P(0, 0.95f), P(-0.22f, -0.22f), col);
        dl->AddQuadFilled(P(-0.95f, 0), P(-0.22f, -0.22f), P(0.95f, 0), P(0.22f, 0.22f), col); break;
    case IG::Text: txt("T", s * 1.05f); break;
    case IG::Film: dl->AddRect(P(-0.75f, -0.85f), P(0.75f, 0.85f), col, 0, 0, t);
        for (int i = 0; i < 3; i++) { dl->AddRectFilled(P(-0.7f, -0.6f + i * 0.5f), P(-0.45f, -0.4f + i * 0.5f), col); dl->AddRectFilled(P(0.45f, -0.6f + i * 0.5f), P(0.7f, -0.4f + i * 0.5f), col); } break;
    case IG::Scene: ui::drawIcon(dl, Icon::Scene, c, s, col); break;
    case IG::Hit: dl->AddRect(P(-0.85f, -0.85f), P(0.25f, 0.25f), col, 0, 0, t); dl->AddRectFilled(P(-0.25f, -0.25f), P(0.85f, 0.85f), col); break;
    case IG::Grid: ui::drawIcon(dl, Icon::Grid, c, s, col); break;
    case IG::Jump: dl->AddLine(P(0, 0.85f), P(0, -0.5f), col, t); dl->AddTriangleFilled(P(-0.55f, -0.3f), P(0.55f, -0.3f), P(0, -0.95f), col);
        dl->AddLine(P(-0.8f, 0.85f), P(0.8f, 0.85f), col, t); break;
    case IG::Fall: dl->AddCircleFilled(P(0, -0.45f), h * 0.35f, col); dl->AddLine(P(-0.85f, 0.8f), P(0.85f, 0.8f), col, t);
        dl->AddLine(P(0, 0.0f), P(0, 0.5f), col, t); break;
    case IG::Button: dl->AddRectFilled(P(-0.9f, -0.45f), P(0.9f, 0.45f), col, h * 0.3f); break;
    case IG::Dice: dl->AddRect(P(-0.8f, -0.8f), P(0.8f, 0.8f), col, 3, 0, t); dl->AddCircleFilled(P(-0.35f, -0.35f), h * 0.13f, col);
        dl->AddCircleFilled(c, h * 0.13f, col); dl->AddCircleFilled(P(0.35f, 0.35f), h * 0.13f, col); break;
    case IG::Mail: dl->AddRect(P(-0.9f, -0.6f), P(0.9f, 0.6f), col, 1, 0, t); dl->AddLine(P(-0.9f, -0.6f), P(0, 0.1f), col, t); dl->AddLine(P(0, 0.1f), P(0.9f, -0.6f), col, t); break;
    case IG::Pause: dl->AddRectFilled(P(-0.6f, -0.75f), P(-0.15f, 0.75f), col); dl->AddRectFilled(P(0.15f, -0.75f), P(0.6f, 0.75f), col); break;
    case IG::Wait: dl->AddTriangle(P(-0.6f, -0.85f), P(0.6f, -0.85f), P(0, 0), col, t); dl->AddTriangleFilled(P(-0.6f, 0.85f), P(0.6f, 0.85f), P(0, 0), col); break;
    case IG::Curve: dl->AddBezierCubic(P(-0.9f, 0.8f), P(0.0f, 0.8f), P(0.0f, -0.8f), P(0.9f, -0.8f), col, t); dl->AddCircleFilled(P(0.9f, -0.8f), h * 0.15f, col); break;
    case IG::Shake: for (int i = 0; i < 3; i++) dl->AddLine(P(-0.8f + i * 0.55f, -0.7f), P(-0.55f + i * 0.55f, 0.7f), col, t); break;
    case IG::Save: ui::drawIcon(dl, Icon::Save, c, s, col); break;
    case IG::Color: dl->AddCircleFilled(P(-0.3f, -0.25f), h * 0.42f, col); dl->AddCircle(P(0.3f, -0.25f), h * 0.42f, col, 12, t); dl->AddCircle(P(0, 0.3f), h * 0.42f, col, 12, t); break;
    case IG::Speed: for (int i = 0; i < 3; i++) dl->AddLine(P(-0.9f + i * 0.2f, -0.5f + i * 0.5f), P(0.2f + i * 0.2f, -0.5f + i * 0.5f), col, t);
        dl->AddTriangleFilled(P(0.4f, -0.8f), P(0.95f, 0), P(0.4f, 0.8f), col); break;
    case IG::Tag: dl->AddCircleFilled(c, h * 0.4f, col); break;
    }
}

static const json *listFor(const json &p) {
    static const std::map<std::string, std::string> m = {{"key", "KEYS"}, {"mouse", "MOUSE"}, {"axis", "AXES"}, {"easing", "EASINGS"},
                                                         {"transition", "TRANSITIONS"}, {"filter", "FILTERS"}, {"op", "OPS"}};
    std::string t = jstr(p, "t");
    if (t == "select") return p.contains("options") ? &p["options"] : nullptr;
    auto it = m.find(t);
    if (it == m.end()) return nullptr;
    return &InstrDefs::get().d["lists"][it->second];
}

static Seg paramShow(Project &proj, const json &p, const json &v) {
    std::string t = jstr(p, "t"), s = v.is_string() ? v.get<std::string>() : v.is_null() ? "" : v.is_number_integer() ? std::to_string(v.get<long long>()) : v.dump();
    if (v.is_number_float()) { char b[32]; snprintf(b, sizeof b, "%g", v.get<double>()); s = b; }
    if (t == "object") { json *o = findById(proj.doc["objects"], s); return {o ? jstr(*o, "name") : "(objeto)", kObj}; }
    if (t == "scene") { json *sc = findById(proj.doc["scenes"], s); return {sc ? jstr(*sc, "name") : "(escena)", kVal}; }
    if (t == "music") { json *m = proj.doc.contains("music") ? findById(proj.doc["music"], s) : nullptr; return {m ? jstr(*m, "name") : "(audio)", kVal}; }
    if (t == "shader") { json *m = proj.doc.contains("shaders") ? findById(proj.doc["shaders"], s) : nullptr; return {m ? jstr(*m, "name") : "(shader)", kVal}; }
    if (t == "op") { const json &w = InstrDefs::get().d["opWords"]; return {w.contains(s) ? w[s].get<std::string>() : s, kOp}; }
    if (const json *l = listFor(p)) {
        for (auto &o : *l) if (o.is_array() && o.size() >= 2 && jstr(json({{"v", o[0]}}), "v") == s) return {o[1].get<std::string>(), kVal};
        return {s, kVal};
    }
    if (t == "text") return {s.empty() ? "\"\"" : s, kStr};
    return {s.empty() ? "0" : s, kVal};
}

std::vector<Seg> sentence(Project &proj, const std::string &kind, const json &row) {
    std::vector<Seg> out;
    std::string type = jstr(row, "type");
    const json *df = InstrDefs::get().def(kind, type);
    if (!df) { out.push_back({"Instrucción desconocida: " + type, kBad}); return out; }
    json params = row.contains("params") && row["params"].is_object() ? row["params"] : json::object();
    if (type == "jsCode") {
        out.push_back({"Código JavaScript", kTxt});
        std::string code = jstr(params, "code"), first;
        size_t a = 0;
        while (a < code.size()) {
            size_t b = code.find('\n', a); if (b == std::string::npos) b = code.size();
            std::string l = code.substr(a, b - a);
            size_t f = l.find_first_not_of(" \t");
            if (f != std::string::npos && l.compare(f, 2, "//") != 0) { first = l.substr(f); break; }
            a = b + 1;
        }
        if (!first.empty()) out.push_back({"  " + (first.size() > 60 ? first.substr(0, 60) + "..." : first), IM_COL32(201, 195, 255, 255)});
        return out;
    }
    std::map<std::string, const json *> pm;
    for (auto &p : (*df)["params"]) pm[jstr(p, "k")] = &p;
    std::string tpl;
    const json &sn = InstrDefs::get().d["sentences"][kind];
    if (sn.contains(type)) tpl = sn[type].get<std::string>();
    else {
        tpl = jstr(*df, "label");
        if (!(*df)["params"].empty()) { tpl += ": "; bool first = true; for (auto &p : (*df)["params"]) { tpl += (first ? "{" : ", {") + jstr(p, "k") + "}"; first = false; } }
    }
    if (type == "callFunction" && jstr(params, "result").empty()) tpl = "Llamar a {name}({args})";
    tpl = ui::tr(tpl);                                   // la frase entera en el idioma del editor (con sus {huecos})
    size_t i = 0;
    std::string acc;
    while (i < tpl.size()) {
        if (tpl[i] == '{') {
            size_t e = tpl.find('}', i);
            std::string k = e == std::string::npos ? "" : tpl.substr(i + 1, e - i - 1);
            if (e != std::string::npos && pm.count(k)) {
                if (!acc.empty()) { out.push_back({acc, kTxt}); acc.clear(); }
                out.push_back(paramShow(proj, *pm[k], params.contains(k) ? params[k] : json()));
                i = e + 1;
                continue;
            }
        }
        acc += tpl[i++];
    }
    if (!acc.empty()) out.push_back({acc, kTxt});
    return out;
}

std::string sentencePlain(Project &proj, const std::string &kind, const json &row) {
    std::string s = jbool(row, "invert", false) ? "NO " : "";
    for (auto &g : sentence(proj, kind, row)) s += g.text;
    return s;
}

json newInstruction(Project &proj, const std::string &kind, const std::string &type, const std::string &objId) {
    const json *df = InstrDefs::get().def(kind, type);
    json params = json::object();
    if (df) for (auto &p : (*df)["params"]) {
        std::string k = jstr(p, "k"), t = jstr(p, "t");
        if (p.contains("d")) { params[k] = p["d"]; continue; }
        json v = "";
        if (t == "object") { json objs = sceneObjects(proj, g_sheetScene); v = !objs.empty() ? jstr(objs[0], "id") : ""; }
        else if (t == "music") v = proj.doc.contains("music") && !proj.doc["music"].empty() ? jstr(proj.doc["music"][0], "id") : "";
        else if (t == "shader") v = proj.doc.contains("shaders") && !proj.doc["shaders"].empty() ? jstr(proj.doc["shaders"][0], "id") : "";
        else if (t == "scene") v = !proj.doc["scenes"].empty() ? jstr(proj.doc["scenes"][0], "id") : "";
        else if (t == "key") v = "arrowright";
        else if (t == "mouse") v = "left";
        else if (t == "op") v = "eq";
        else if (t == "axis") v = "x";
        else if (t == "easing") v = "easeOut";
        else if (t == "transition") v = "fade";
        else if (t == "filter") v = "none";
        params[k] = v;
    }
    if (!objId.empty() && params.contains("object")) params["object"] = objId;
    json row = {{"type", type}, {"invert", false}, {"enabled", true}, {"params", params}};
    if (df && jbool(*df, "group", false)) row["sub"] = json::array();
    return row;
}

/* ---------------- comprobacion de expresiones (la misma sintaxis que el runtime) ---------------- */
// $var, $array[i] o el nombre sin $ (variables), Objeto.Propiedad, Fn.funcion(...), cursor.X(), textos entre
// comillas, funciones de EXPR_FN y operadores de JS. Solo se avisa de lo que seguro es un error.
std::string checkExpression(Project &proj, const std::string &expr0) {
    std::string e = expr0;
    size_t a = e.find_first_not_of(" \t"), b = e.find_last_not_of(" \t");
    if (a == std::string::npos) return "";
    e = e.substr(a, b - a + 1);
    char *end = nullptr;
    strtod(e.c_str(), &end);
    if (end && *end == 0) return "";
    for (size_t p = e.find("{g:"); p != std::string::npos; p = e.find("{g:")) {
        size_t q = e.find('}', p);
        if (q == std::string::npos) return "Falta cerrar {g:...}";
        e.replace(p, q - p + 1, "0");
    }
    static const std::vector<std::string> props = {"X", "Y", "Ángulo", "Angulo", "Ancho", "Alto", "Opacidad", "VelX", "VelY", "Salud", "x", "y", "angle", "width", "height", "opacity", "vx", "vy", "health"};
    auto ident = [](unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; };
    std::vector<char> stack;
    for (size_t i = 0; i < e.size();) {
        unsigned char c = (unsigned char)e[i];
        if (std::isspace(c)) { i++; continue; }
        if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < e.size() && e[j] != (char)c) j += e[j] == '\\' ? 2 : 1;
            if (j >= e.size()) return "Falta cerrar las comillas de un texto.";
            i = j + 1; continue;
        }
        if (std::isdigit(c) || (c == '.' && i + 1 < e.size() && std::isdigit((unsigned char)e[i + 1]))) {
            while (i < e.size() && (std::isalnum((unsigned char)e[i]) || e[i] == '.')) i++;
            continue;
        }
        if (c == '$' || std::isalpha(c) || c == '_' || c >= 0x80) {
            bool dollar = c == '$';
            size_t s0 = dollar ? i + 1 : i;
            i = s0;
            while (i < e.size() && ident((unsigned char)e[i])) i++;
            std::string id = e.substr(s0, i - s0);
            if (dollar) {
                if (id.empty()) return "Falta el nombre de la variable después de $.";
                while (i + 1 < e.size() && e[i] == '.' && ident((unsigned char)e[i + 1])) { i++; while (i < e.size() && ident((unsigned char)e[i])) i++; }
                continue;
            }
            if (i + 1 < e.size() && e[i] == '.' && ident((unsigned char)e[i + 1])) {
                size_t ps = ++i;
                while (i < e.size() && ident((unsigned char)e[i])) i++;
                std::string pr = e.substr(ps, i - ps);
                if (id == "Math" || id == "cursor") continue;
                if (id == "Fn") {
                    size_t k = i; while (k < e.size() && e[k] == ' ') k++;
                    if (k >= e.size() || e[k] != '(') return "Fn." + pr + " es una función: escribe Fn." + pr + "(...).";
                    bool found = false;
                    for (auto &ev : proj.doc["events"]) if (jstr(ev, "type") == "function" && jstr(ev, "name") == pr) found = true;
                for (auto &sc : proj.doc["scenes"]) if (sc.contains("events")) for (auto &ev : sc["events"]) if (jstr(ev, "type") == "function" && jstr(ev, "name") == pr) found = true;
                    if (!found) return "La función '" + pr + "' no existe. Créala con el botón Función de la hoja de eventos.";
                    continue;
                }
                json *obj = nullptr;
                for (auto &o : proj.doc["objects"]) if (jstr(o, "name") == id) obj = &o;
                if (!obj) return "El objeto '" + id + "' no existe. Usa el nombre de un objeto de la lista.";
                bool ok = std::find(props.begin(), props.end(), pr) != props.end();
                if (!ok && obj->contains("vars")) for (auto &v : (*obj)["vars"]) if (jstr(v, "name") == pr) ok = true;
                if (!ok) return "'" + id + "." + pr + "' no existe. Propiedades: X, Y, Ángulo, Ancho, Alto, Opacidad, VelX, VelY, Salud" + " o una variable del objeto.";
                size_t k = i; while (k < e.size() && e[k] == ' ') k++;
                if (k < e.size() && e[k] == '(') return "'" + id + "." + pr + "' no es una función. Quita los paréntesis.";
                continue;
            }
            for (auto &o : proj.doc["objects"])
                if (jstr(o, "name") == id) return "Usa " + id + ".X (o Y, Ancho...) para leer una propiedad de " + id + ".";
            continue;   // funcion matematica o variable sin $
        }
        if (c == '(' || c == '[') stack.push_back((char)c);
        else if (c == ')' || c == ']') {
            if (stack.empty()) return c == ')' ? "Sobra un paréntesis de cierre." : "Sobra un corchete de cierre.";
            if ((c == ')') != (stack.back() == '(')) return "Los paréntesis y corchetes no casan.";
            stack.pop_back();
        } else if (!strchr("+-*/%,<>=!&|?:^~.", (char)c)) return "Hay caracteres que no van en una expresión.";
        i++;
    }
    if (!stack.empty()) return stack.back() == '(' ? "Falta cerrar un paréntesis." : "Falta cerrar un corchete.";
    return "";
}

/* ---------------- texto enriquecido con ajuste de linea ---------------- */
static float richLayout(ImDrawList *dl, ImVec2 pos, float wrapW, const std::vector<Seg> &segs, bool dim) {
    ImFont *f = ImGui::GetFont();
    float fs = ImGui::GetFontSize(), lh = ImGui::GetTextLineHeight(), x = 0, y = 0;
    for (auto &s : segs) {
        size_t i = 0;
        while (i < s.text.size()) {
            size_t j = i;
            while (j < s.text.size() && s.text[j] == ' ') j++;
            while (j < s.text.size() && s.text[j] != ' ') j++;
            std::string w = s.text.substr(i, j - i);
            float ww = f->CalcTextSizeA(fs, FLT_MAX, 0, w.c_str()).x;
            if (x > 0 && x + ww > wrapW) {
                x = 0; y += lh;
                size_t k = w.find_first_not_of(' ');
                if (k == std::string::npos) { i = j; continue; }
                w = w.substr(k); ww = f->CalcTextSizeA(fs, FLT_MAX, 0, w.c_str()).x;
            }
            if (dl) {
                ImU32 c = dim ? (s.col & 0x00FFFFFF) | 0x70000000 : s.col;
                dl->AddText(f, fs, ImVec2(pos.x + x, pos.y + y), c, w.c_str());
                if (s.col != kTxt && !dim) dl->AddText(f, fs, ImVec2(pos.x + x + 0.6f, pos.y + y), c, w.c_str());   // valores en negrita
            }
            x += ww; i = j;
        }
    }
    return y + lh;
}

/* ---------------- operaciones ---------------- */
json &EventsEditor::events(Project &proj) { return sceneEvents(proj, sceneId); }

std::string EventsEditor::title(Project &proj) {
    std::string nm = sceneName(proj, sceneId);
    return (sceneId.empty() || nm.empty() ? std::string("Eventos comunes") : "Eventos: " + nm) + "###ev_" + sceneId;
}

int EventsEditor::blockEnd(json &evs, int i) {
    int lvl = (int)jnum(evs[i], "level", 0), e = i + 1;
    while (e < (int)evs.size() && !evs[e].contains("kind") && (int)jnum(evs[e], "level", 0) > lvl) e++;
    return e;
}

int EventsEditor::addEvent(Project &proj, const std::string &type, bool asSub) {
    json &evs = events(proj);
    json ev = {{"id", makeUid("ev")}, {"enabled", true}, {"conditions", json::array()}, {"actions", json::array()}};
    if (type == "repeat") { ev["type"] = "repeat"; ev["count"] = "3"; }
    if (type == "forEach") { json objs = sceneObjects(proj, sceneId); ev["type"] = "forEach"; ev["object"] = objs.empty() ? "" : jstr(objs[0], "id"); }
    if (type == "while") { ev["type"] = "while"; ev["whileConds"] = json::array(); }
    if (type == "function") { ev["type"] = "function"; ev["name"] = "MiFuncion"; ev["params"] = "a, b"; asSub = false; }
    int at = (int)evs.size();
    if (sel >= 0 && sel < (int)evs.size() && !evs[sel].contains("kind")) {
        at = blockEnd(evs, sel);
        ev["level"] = (int)jnum(evs[sel], "level", 0) + (asSub ? 1 : 0);
        if (evs[sel].contains("folder") && !evs[sel]["folder"].is_null()) ev["folder"] = evs[sel]["folder"];
    } else if (sel >= 0 && sel < (int)evs.size() && jstr(evs[sel], "kind") == "folder") {
        at = sel + 1; ev["folder"] = jstr(evs[sel], "id");
    } else if (asSub) return -1;
    evs.insert(evs.begin() + at, ev);
    sel = at; selEv = -1;
    proj.dirty = true;
    return at;
}

void EventsEditor::addComment(Project &proj) {
    json &evs = events(proj);
    int at = sel >= 0 && sel < (int)evs.size() ? (evs[sel].contains("kind") ? sel + 1 : blockEnd(evs, sel)) : (int)evs.size();
    json c = {{"id", makeUid("nt")}, {"kind", "comment"}, {"text", ""}};
    if (sel >= 0 && sel < (int)evs.size() && evs[sel].contains("folder") && !evs[sel]["folder"].is_null()) c["folder"] = evs[sel]["folder"];
    evs.insert(evs.begin() + at, c);
    sel = at; proj.dirty = true;
}

void EventsEditor::addGroup(Project &proj) {
    json &evs = events(proj);
    int n = 1;
    for (auto &e : evs) if (jstr(e, "kind") == "folder") n++;
    evs.push_back({{"id", makeUid("fd")}, {"kind", "folder"}, {"name", "Grupo " + std::to_string(n)}, {"collapsed", false}});
    sel = (int)evs.size() - 1; proj.dirty = true;
}

void EventsEditor::deleteEvent(Project &proj, int i) {
    json &evs = events(proj);
    if (i < 0 || i >= (int)evs.size()) return;
    if (jstr(evs[i], "kind") == "folder") {             // los eventos del grupo quedan fuera del grupo
        std::string id = jstr(evs[i], "id");
        for (auto &e : evs) if (jstr(e, "folder") == id) e.erase("folder");
        evs.erase(evs.begin() + i);
    } else {
        int e = evs[i].contains("kind") ? i + 1 : blockEnd(evs, i);
        evs.erase(evs.begin() + i, evs.begin() + e);
    }
    sel = -1; selEv = -1; proj.dirty = true;
}

json *EventsEditor::instrList(json &ev, const std::string &path, bool create) {
    std::vector<std::string> parts;
    size_t a = 0;
    while (a <= path.size()) { size_t b = path.find('.', a); if (b == std::string::npos) b = path.size(); parts.push_back(path.substr(a, b - a)); a = b + 1; }
    const char *rk = parts[0] == "a" ? "actions" : parts[0] == "w" ? "whileConds" : "conditions";
    if (!ev.contains(rk) || !ev[rk].is_array()) { if (!create) return nullptr; ev[rk] = json::array(); }
    json *l = &ev[rk];
    for (size_t k = 1; k < parts.size(); k++) {
        int i = atoi(parts[k].c_str());
        if (i < 0 || i >= (int)l->size()) return nullptr;
        json &r = (*l)[i];
        if (!r.contains("sub") || !r["sub"].is_array()) { if (!create) return nullptr; r["sub"] = json::array(); }
        l = &r["sub"];
    }
    return l;
}

json *EventsEditor::instrAt(json &ev, const std::string &path, json **list, int *idx) {
    size_t d = path.rfind('.');
    if (d == std::string::npos) return nullptr;
    json *l = instrList(ev, path.substr(0, d), false);
    int i = atoi(path.c_str() + d + 1);
    if (!l || i < 0 || i >= (int)l->size()) return nullptr;
    if (list) *list = l;
    if (idx) *idx = i;
    return &(*l)[i];
}

/* ---------------- dialogo de instruccion ---------------- */
void EventsEditor::openDialog(Project &proj, const std::string &kind, int ev, const std::string &path, bool edit) {
    dlg = Dialog{};
    dlg.open = true; dlg.justOpened = true; dlg.kind = kind; dlg.ev = ev; dlg.path = path; dlg.edit = edit; dlg.cat = InstrDefs::get().d["gdCats"][1];
    json &evs = events(proj);
    if (edit && ev >= 0 && ev < (int)evs.size()) {
        if (json *r = instrAt(evs[ev], path, nullptr, nullptr)) { dlg.row = *r; dlg.form = true; dlg.objId = jstr((*r)["params"], "object"); }
    }
    json objs = sceneObjects(proj, sceneId);
    if (dlg.objId.empty() && !objs.empty()) dlg.objId = jstr(objs[0], "id");
}

void EventsEditor::dialogPick(Project &proj, const std::string &type) {
    json prev = dlg.row;
    json row = newInstruction(proj, dlg.kind, type, dlg.tab == 0 ? dlg.objId : "");
    if (prev.is_object() && prev.contains("params"))
        for (auto it = row["params"].begin(); it != row["params"].end(); ++it)
            if (prev["params"].contains(it.key()) && it.key() != "code" && !(it.key() == "object" && dlg.tab == 0)) it.value() = prev["params"][it.key()];
    if (prev.is_object()) { row["invert"] = jbool(prev, "invert", false); if (prev.contains("sub") && row.contains("sub")) row["sub"] = prev["sub"]; }
    dlg.row = row; dlg.form = true;
}

void EventsEditor::dialogAccept(Project &proj) {
    json &evs = events(proj);
    if (dlg.ev < 0 || dlg.ev >= (int)evs.size() || !dlg.row.is_object()) { dlg.open = false; return; }
    json &ev = evs[dlg.ev];
    if (dlg.edit) {
        json *list = nullptr; int idx = 0;
        if (json *r = instrAt(ev, dlg.path, &list, &idx)) {
            if (r->contains("sub") && !dlg.row.contains("sub") && InstrDefs::get().def("cond", jstr(dlg.row, "type")) &&
                jbool(*InstrDefs::get().def("cond", jstr(dlg.row, "type")), "group", false)) dlg.row["sub"] = (*r)["sub"];
            (*list)[idx] = dlg.row;
        }
    } else if (json *l = instrList(ev, dlg.path, true)) l->push_back(dlg.row);
    proj.dirty = true;
    dlg.open = false;
}

static bool isObjDef(const json &df) { return !df["params"].empty() && jstr(df["params"][0], "k") == "object"; }

void EventsEditor::dialogList(Project &proj) {
    InstrDefs &D = InstrDefs::get();
    auto defList = D.all(dlg.kind);
    if (ImGui::BeginTabBar("##dtabs")) {
        if (ImGui::BeginTabItem("Objetos", nullptr, dlg.justOpened && dlg.tab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) { dlg.tab = 0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(dlg.kind == "cond" ? "Otras condiciones" : "Otras acciones")) { dlg.tab = 1; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    char qb[96]; snprintf(qb, sizeof qb, "%s", dlg.q.c_str());
    ImGui::SetNextItemWidth(-1);
    if (dlg.justOpened) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputTextWithHint("##q", "Buscar...", qb, sizeof qb)) dlg.q = qb;
    std::string q = dlg.q;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    auto match = [&](const json &df) {
        if (q.empty()) return true;
        std::string s = jstr(df, "label") + " " + jstr(df, "cat") + " " + D.gdCat(df);
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s.find(q) != std::string::npos;
    };
    float h = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 8;
    ImGui::BeginChild("##left", ImVec2(230, h), ImGuiChildFlags_Borders);
    json *obj = findById(proj.doc["objects"], dlg.objId);
    if (dlg.tab == 0) {
        for (auto &o : proj.doc["objects"]) {
            if (!objectInScene(o, sceneId)) continue;   // solo los objetos de la escena de esta hoja y los globales
            std::string id = jstr(o, "id");
            ImGui::PushID(id.c_str());
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##o", id == dlg.objId, 0, ImVec2(0, 26))) dlg.objId = id;
            objectThumbAt(ImGui::GetWindowDrawList(), proj, o, ImVec2(p.x + 2, p.y + 1), 24);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 32, p.y + 4), kTxt, jstr(o, "name").c_str());
            ImGui::PopID();
        }
        if (proj.doc["objects"].empty()) ImGui::TextDisabled("No hay objetos.");
    } else {
        std::map<std::string, int> counts;
        for (auto &kv : defList) if (!jbool(*kv.second, "hidden", false) && !isObjDef(*kv.second)) counts[D.gdCat(*kv.second)]++;
        for (auto &c : D.d["gdCats"]) {
            std::string cn = c.get<std::string>();
            if (!counts[cn]) continue;
            char lbl[96]; snprintf(lbl, sizeof lbl, "%s  (%d)", cn.c_str(), counts[cn]);
            if (ImGui::Selectable(lbl, dlg.cat == cn)) dlg.cat = cn;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##right", ImVec2(0, h), ImGuiChildFlags_Borders);
    std::vector<std::pair<std::string, std::string>> items;    // categoria, clave
    std::vector<std::string> objBh = obj ? objBehaviorTypes(*obj) : std::vector<std::string>();
    std::string objType = obj ? jstr(*obj, "type", "sprite") : "sprite";
    std::map<std::string, const json *> byKey;
    for (auto &kv : defList) {
        const json &df = *kv.second;
        byKey[kv.first] = kv.second;
        if (jbool(df, "hidden", false) || !match(df)) continue;
        if (dlg.tab == 0) {
            if (!isObjDef(df) || !obj) continue;
            if (df.contains("objTypes")) { bool ok = false; for (auto &t : df["objTypes"]) if (t == objType) ok = true; if (!ok) continue; }
            if (df.contains("bh") && std::find(objBh.begin(), objBh.end(), jstr(df, "bh")) == objBh.end()) continue;
            items.push_back({jstr(df, "cat", "General"), kv.first});
        } else {
            if (isObjDef(df) || (q.empty() && D.gdCat(df) != dlg.cat)) continue;
            items.push_back({D.gdCat(df), kv.first});
        }
    }
    std::vector<std::string> order;
    for (auto &i : items) if (std::find(order.begin(), order.end(), i.first) == order.end()) order.push_back(i.first);
    std::stable_sort(order.begin(), order.end(), [](const std::string &a, const std::string &b) { return (a.rfind("Comportamiento", 0) == 0) < (b.rfind("Comportamiento", 0) == 0); });
    for (auto &cat : order) {
        ImGui::SeparatorText(cat.c_str());
        for (auto &i : items) {
            if (i.first != cat) continue;
            ImVec2 ip = ImGui::GetCursorScreenPos();
            ImGui::PushID(i.second.c_str());
            if (ImGui::Selectable("##it", false)) dialogPick(proj, i.second);
            ImGui::PopID();
            const json &idf = *byKey[i.second];
            float lh = ImGui::GetTextLineHeight();
            drawInstrIcon(ImGui::GetWindowDrawList(), i.second, jstr(idf, "cat"), ImVec2(ip.x + 9, ip.y + lh / 2), 14, catColor(D.gdCat(idf)));
            ImGui::GetWindowDrawList()->AddText(ImVec2(ip.x + 22, ip.y), kTxt, jstr(idf, "label").c_str());
        }
    }
    if (items.empty()) ImGui::TextDisabled(dlg.tab == 0 && !obj ? "Elige un objeto a la izquierda." : "Sin resultados.");
    ImGui::EndChild();
}

static bool paramField(Project &proj, const json &p, json &params) {
    std::string k = jstr(p, "k"), t = jstr(p, "t");
    json &v = params[k];
    std::string cur = v.is_string() ? v.get<std::string>() : v.is_null() ? "" : v.is_number_integer() ? std::to_string(v.get<long long>()) : v.dump();
    if (v.is_number_float()) { char b[32]; snprintf(b, sizeof b, "%g", v.get<double>()); cur = b; }
    ImGui::PushID(k.c_str());
    ImGui::SetNextItemWidth(-1);
    bool ch = false;
    auto combo = [&](json &list, const char *empty) {
        std::string curName = empty;
        for (auto &x : list) if (jstr(x, "id") == cur) curName = jstr(x, "name");
        if (ImGui::BeginCombo("##c", curName.c_str())) {
            for (auto &x : list) if (ImGui::Selectable(jstr(x, "name").c_str(), jstr(x, "id") == cur)) { v = jstr(x, "id"); ch = true; }
            ImGui::EndCombo();
        }
    };
    if (t == "object") { json objs = sceneObjects(proj, g_sheetScene); combo(objs, "(elige un objeto)"); }
    else if (t == "scene") combo(proj.doc["scenes"], "(elige una escena)");
    else if (t == "music") { if (!proj.doc.contains("music")) proj.doc["music"] = json::array(); combo(proj.doc["music"], "(sin audio en el proyecto)"); }
    else if (t == "shader") { if (!proj.doc.contains("shaders")) proj.doc["shaders"] = json::array(); combo(proj.doc["shaders"], "(sin shaders en el proyecto)"); }
    else if (const json *l = listFor(p)) {
        std::string curName = cur;
        for (auto &o : *l) if (jstr(json({{"v", o[0]}}), "v") == cur) curName = o[1].get<std::string>();
        if (ImGui::BeginCombo("##l", curName.c_str())) {
            for (auto &o : *l) { std::string ov = jstr(json({{"v", o[0]}}), "v"); if (ImGui::Selectable(o[1].get<std::string>().c_str(), ov == cur)) { v = o[0]; ch = true; } }
            ImGui::EndCombo();
        }
    } else if (t == "code") {
        std::string code = cur;
        if (codeFieldStr("##code", code, ImVec2(-1, 200), CodeLang::JS, true)) { v = code; ch = true; }
    } else {
        char buf[1024]; snprintf(buf, sizeof buf, "%s", cur.c_str());
        if (ImGui::InputText("##t", buf, sizeof buf)) {
            char *e = nullptr; double d = strtod(buf, &e);
            v = (t == "number" && buf[0] && e && *e == 0) ? (d == (long long)d ? json((long long)d) : json(d)) : json(std::string(buf));
            ch = true;
        }
        if (t == "number") {
            std::string err = checkExpression(proj, buf);
            if (!err.empty()) { ImGui::PushTextWrapPos(); ImGui::TextColored(ImVec4(1, 0.56f, 0.55f, 1), "%s", err.c_str()); ImGui::PopTextWrapPos(); }
        }
    }
    ImGui::PopID();
    return ch;
}

void EventsEditor::dialogForm(Project &proj) {
    InstrDefs &D = InstrDefs::get();
    const json *df = D.def(dlg.kind, jstr(dlg.row, "type"));
    if (!df) { ImGui::TextColored(ImVec4(1, .6f, .6f, 1), "Instrucción desconocida"); return; }
    ImGui::TextDisabled("%s", isObjDef(*df) ? jstr(*df, "cat", "Objetos").c_str() : D.gdCat(*df).c_str());
    ImGui::PushFont(ui::boldFont(), 18.0f); ImGui::TextUnformatted(jstr(*df, "label").c_str()); ImGui::PopFont();
    // frase en vivo
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    std::vector<Seg> segs;
    if (jbool(dlg.row, "invert", false)) segs.push_back({"NO ", IM_COL32(240, 96, 93, 255)});
    for (auto &s : sentence(proj, dlg.kind, dlg.row)) segs.push_back(s);
    float th = richLayout(nullptr, ImVec2(0, 0), w - 24, segs, false);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + th + 18), IM_COL32(24, 24, 31, 255), 8);
    dl->AddRect(p, ImVec2(p.x + w, p.y + th + 18), IM_COL32(62, 62, 75, 255), 8);
    richLayout(dl, ImVec2(p.x + 12, p.y + 9), w - 24, segs, false);
    ImGui::Dummy(ImVec2(w, th + 24));
    if (jbool(*df, "group", false)) ImGui::TextWrapped("Después de aceptar, añade las subcondiciones dentro de este bloque en la hoja de eventos.");
    if (jstr(dlg.row, "type") == "triggerOnce") ImGui::TextWrapped("Las acciones solo se ejecutan el primer fotograma en que las condiciones anteriores se cumplen, y otra vez solo después de que dejen de cumplirse.");
    if (!dlg.row.contains("params") || !dlg.row["params"].is_object()) dlg.row["params"] = json::object();
    const json &pn = D.d["paramNames"];
    if (ImGui::BeginTable("##params", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, 170);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        bool first = true;
        for (auto &prm : (*df)["params"]) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string k = jstr(prm, "k"), lbl = pn.contains(k) ? pn[k].get<std::string>() : (jstr(prm, "l").empty() ? k : jstr(prm, "l"));
            ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(lbl.c_str());
            ImGui::TableNextColumn();
            if (first && dlg.justOpened) ImGui::SetKeyboardFocusHere();
            first = false;
            paramField(proj, prm, dlg.row["params"]);
        }
        ImGui::EndTable();
    }
    if ((*df)["params"].empty() && !jbool(*df, "group", false) && jstr(dlg.row, "type") != "triggerOnce") ImGui::TextDisabled("Esta instrucción no tiene parámetros.");
    if (dlg.kind == "cond" && !jbool(*df, "group", false) && jstr(dlg.row, "type") != "triggerOnce") {
        bool inv = jbool(dlg.row, "invert", false);
        ImGui::Spacing();
        if (ImGui::Checkbox("Invertir la condición (se cumple cuando NO pasa)", &inv)) dlg.row["invert"] = inv;
    }
}

void EventsEditor::drawDialog(Project &proj) {
    if (!dlg.open) return;
    std::string titleS = std::string(dlg.kind == "cond" ? "Condición" : "Acción") + (dlg.edit ? " (editar)" : "") + "###instrdlg";
    const char *title = titleS.c_str();
    ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Appearing);
    bool keep = true;
    if (!ui::beginModal(title, &keep)) return;
    if (!keep) { dlg.open = false; ImGui::EndPopup(); return; }
    float footer = ImGui::GetFrameHeightWithSpacing() + 10;
    ImGui::BeginChild("##dbody", ImVec2(0, -footer));
    if (dlg.form) dialogForm(proj); else dialogList(proj);
    ImGui::EndChild();
    ImGui::Separator();
    if (dlg.form && ImGui::Button("< Elegir otra instrucción")) dlg.form = false;
    float bw = 110;
    ImGui::SameLine(ImGui::GetWindowWidth() - bw * 2 - 26);
    if (ImGui::Button("Cancelar", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) dlg.open = false;
    ImGui::SameLine();
    ImGui::BeginDisabled(!dlg.form);
    bool enter = dlg.form && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    if (ui::primaryButton("Aceptar", ImVec2(bw, 0)) || enter || (dlg.form && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) dialogAccept(proj);
    ImGui::EndDisabled();
    dlg.justOpened = false;
    if (!dlg.open) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

/* ---------------- hoja ---------------- */
bool EventsEditor::matchesSearch(Project &proj, const json &ev) {
    std::string q = search;
    if (q.empty()) return true;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    std::string all = jstr(ev, "text") + " " + jstr(ev, "name");
    std::function<void(const json &, const char *)> add = [&](const json &l, const char *kind) {
        if (!l.is_array()) return;
        for (auto &r : l) { all += " " + sentencePlain(proj, kind, r); if (r.contains("sub")) add(r["sub"], "cond"); }
    };
    if (ev.contains("conditions")) add(ev["conditions"], "cond");
    if (ev.contains("whileConds")) add(ev["whileConds"], "cond");
    if (ev.contains("actions")) add(ev["actions"], "act");
    std::transform(all.begin(), all.end(), all.begin(), ::tolower);
    return all.find(q) != std::string::npos;
}

struct InsPayload { int ev; char path[48]; char kind[8]; };
static void insertRange(json &arr, int at, const std::vector<json> &items) {
    for (size_t k = 0; k < items.size(); k++) arr.insert(arr.begin() + at + (int)k, items[k]);
}
static std::vector<std::function<void()>> g_post;            // cambios de estructura al final del fotograma

bool EventsEditor::drawInstr(Project &proj, json &ev, int evIdx, const std::string &kind, const std::string &path, json &row, float width) {
    ImGui::PushID(path.c_str());
    const json *df = InstrDefs::get().def(kind, jstr(row, "type"));
    bool disabled = !jbool(row, "enabled", true) || !jbool(ev, "enabled", true);
    std::vector<Seg> segs;
    if (jbool(row, "invert", false)) segs.push_back({"NO  ", IM_COL32(255, 255, 255, 255)});
    for (auto &s : sentence(proj, kind, row)) segs.push_back(s);
    float textW = width - 22 - 54;
    float th = richLayout(nullptr, ImVec2(0, 0), textW, segs, false);
    float h = th + 8;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##ins", ImVec2(width, h));
    bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem);
    bool selected = selEv == evIdx && selPath == path;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) { selEv = evIdx; selPath = path; sel = evIdx; }
    if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openDialog(proj, kind, evIdx, path, true);
    if (ImGui::BeginDragDropSource()) {
        InsPayload pl{evIdx, {0}, {0}};
        snprintf(pl.path, sizeof pl.path, "%s", path.c_str()); snprintf(pl.kind, sizeof pl.kind, "%s", kind.c_str());
        ImGui::SetDragDropPayload("S2D_INS", &pl, sizeof pl);
        ImGui::TextUnformatted(sentencePlain(proj, kind, row).c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *pd = ImGui::AcceptDragDropPayload("S2D_INS")) {
            InsPayload src = *(const InsPayload *)pd->Data;
            bool before = ImGui::GetIO().MousePos.y < p.y + h / 2;
            std::string dpath = path, dkind = kind; int dev = evIdx;
            if (src.kind == dkind) g_post.push_back([this, &proj, src, dpath, dev, before]() {
                json &evs = events(proj);
                if (src.ev >= (int)evs.size() || dev >= (int)evs.size()) return;
                json *sl = nullptr, *dlst = nullptr; int si = 0, di = 0;
                json *sr = instrAt(evs[src.ev], src.path, &sl, &si);
                json *dr = instrAt(evs[dev], dpath, &dlst, &di);
                if (!sr || !dr || sr == dr) return;
                json moving = *sr, target = *dr;
                if (moving.contains("sub") && moving["sub"].dump().find(target.dump()) != std::string::npos && dpath.find(std::string(src.path) + ".") == 0) return;
                sl->erase(sl->begin() + si);
                int at = -1;
                for (int k = 0; k < (int)dlst->size(); k++) if ((*dlst)[k] == target) { at = k; break; }
                if (at < 0) at = (int)dlst->size(); else if (!before) at++;
                dlst->insert(dlst->begin() + at, moving);
                proj.dirty = true; selEv = -1;
            });
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("##ictx")) {
        bool canInv = kind == "cond" && df && !jbool(*df, "group", false) && jstr(row, "type") != "triggerOnce";
        if (ImGui::MenuItem("Editar", "Doble clic")) openDialog(proj, kind, evIdx, path, true);
        if (ImGui::MenuItem("Invertir la condición", nullptr, jbool(row, "invert", false), canInv)) row["invert"] = !jbool(row, "invert", false);
        if (ImGui::MenuItem("Activar / desactivar", nullptr, !jbool(row, "enabled", true))) row["enabled"] = !jbool(row, "enabled", true);
        ImGui::Separator();
        std::string pth = path; int ei = evIdx;
        auto withList = [this, &proj, pth, ei](std::function<void(json &, int)> fn) {
            g_post.push_back([this, &proj, pth, ei, fn]() {
                json &evs = events(proj);
                if (ei >= (int)evs.size()) return;
                json *l = nullptr; int i = 0;
                if (instrAt(evs[ei], pth, &l, &i)) { fn(*l, i); proj.dirty = true; selEv = -1; }
            });
        };
        if (ImGui::MenuItem("Copiar")) { clipInstr = row; clipKind = kind; }
        if (ImGui::MenuItem("Cortar")) { clipInstr = row; clipKind = kind; withList([](json &l, int i) { l.erase(l.begin() + i); }); }
        if (ImGui::MenuItem("Pegar debajo", nullptr, false, clipInstr.is_object() && clipKind == kind)) { json c = clipInstr; withList([c](json &l, int i) { l.insert(l.begin() + i + 1, c); }); }
        if (ImGui::MenuItem("Duplicar")) { json c = row; withList([c](json &l, int i) { l.insert(l.begin() + i + 1, c); }); }
        ImGui::Separator();
        if (ImGui::MenuItem("Eliminar", "Supr")) withList([](json &l, int i) { l.erase(l.begin() + i); });
        ImGui::EndPopup();
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), IM_COL32(157, 125, 255, 40), 6);
    else if (hov) dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), IM_COL32(255, 255, 255, 12), 6);
    if (selected) dl->AddRect(p, ImVec2(p.x + width, p.y + h), IM_COL32(157, 125, 255, 140), 6);
    drawInstrIcon(dl, jstr(row, "type"), df ? jstr(*df, "cat") : "", ImVec2(p.x + 10, p.y + 4 + ImGui::GetTextLineHeight() / 2), 15,
                  df ? catColor(InstrDefs::get().gdCat(*df)) : kBad);
    if (jbool(row, "invert", false)) {
        float nw = ImGui::CalcTextSize("NO").x;
        dl->AddRectFilled(ImVec2(p.x + 19, p.y + 5), ImVec2(p.x + 25 + nw, p.y + 3 + ImGui::GetTextLineHeight()), IM_COL32(240, 96, 93, 255), 4);
    }
    richLayout(dl, ImVec2(p.x + 22, p.y + 4), textW, segs, disabled);
    if (disabled) dl->AddLine(ImVec2(p.x + 22, p.y + 4 + ImGui::GetTextLineHeight() / 2), ImVec2(p.x + 22 + std::min(textW, ImGui::CalcTextSize(sentencePlain(proj, kind, row).c_str()).x), p.y + 4 + ImGui::GetTextLineHeight() / 2), IM_COL32(200, 200, 210, 120));
    if (hov || selected) {
        ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(p.x + width - 52, p.y + 1));
        if (ui::iconButton("ed", Icon::Pencil, false, "Editar", 22)) openDialog(proj, kind, evIdx, path, true);
        ImGui::SameLine(0, 2);
        if (ui::iconButton("rm", Icon::Trash, false, "Eliminar", 22)) {
            std::string pth = path; int ei = evIdx;
            g_post.push_back([this, &proj, pth, ei]() {
                json &evs = events(proj); json *l = nullptr; int i = 0;
                if (ei < (int)evs.size() && instrAt(evs[ei], pth, &l, &i)) { l->erase(l->begin() + i); proj.dirty = true; selEv = -1; }
            });
        }
        ImGui::SetCursorScreenPos(after);
        ImGui::Dummy(ImVec2(0, 0));
    }
    // bloque O / Y / No: subcondiciones con sangria
    if (df && jbool(*df, "group", false)) {
        if (!row.contains("sub") || !row["sub"].is_array()) row["sub"] = json::array();
        ImVec2 sp = ImGui::GetCursorScreenPos();
        ImGui::Indent(18);
        drawInstrColumn(proj, ev, evIdx, "cond", path, row["sub"], width - 18);
        ImGui::Unindent(18);
        ImVec2 ep = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(sp.x + 8, sp.y), ImVec2(sp.x + 8, ep.y - 4), IM_COL32(106, 90, 168, 255), 2);
    }
    ImGui::PopID();
    return true;
}

void EventsEditor::drawInstrColumn(Project &proj, json &ev, int evIdx, const std::string &kind, const std::string &root, json &list, float width) {
    for (int j = 0; j < (int)list.size(); j++) drawInstr(proj, ev, evIdx, kind, root + "." + std::to_string(j), list[j], width);
    bool sub = root.find('.') != std::string::npos;
    std::string label = std::string("+ ") + (sub ? "Añadir una subcondición" : kind == "cond" ? "Añadir una condición" : "Añadir una acción");
    ImGui::PushID(root.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(187 / 255.f, 168 / 255.f, 1, 0.9f));
    if (ImGui::SmallButton(label.c_str())) { sel = evIdx; openDialog(proj, kind, evIdx, root, false); }
    ImGui::PopStyleColor(3);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *pd = ImGui::AcceptDragDropPayload("S2D_INS")) {
            InsPayload src = *(const InsPayload *)pd->Data;
            std::string droot = root; int dev = evIdx;
            if (src.kind == kind) g_post.push_back([this, &proj, src, droot, dev]() {
                json &evs = events(proj);
                if (src.ev >= (int)evs.size() || dev >= (int)evs.size()) return;
                json *sl = nullptr; int si = 0;
                json *sr = instrAt(evs[src.ev], src.path, &sl, &si);
                if (!sr || droot.find(std::string(src.path)) == 0) return;      // no dentro de si misma
                json moving = *sr;
                sl->erase(sl->begin() + si);
                if (json *dlst = instrList(evs[dev], droot, true)) dlst->push_back(moving);
                proj.dirty = true; selEv = -1;
            });
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
}

void EventsEditor::eventContext(Project &proj, json &evs, int i) {
    bool isEv = !evs[i].contains("kind");
    if (ImGui::MenuItem("Añadir un evento debajo")) { sel = i; g_post.push_back([this, &proj]() { addEvent(proj, "", false); }); }
    if (isEv && ImGui::MenuItem("Añadir un subevento")) { sel = i; g_post.push_back([this, &proj]() { addEvent(proj, "", true); }); }
    if (ImGui::MenuItem("Añadir un comentario debajo")) { sel = i; g_post.push_back([this, &proj]() { addComment(proj); }); }
    ImGui::Separator();
    if (isEv && ImGui::MenuItem("Activar / desactivar", nullptr, !jbool(evs[i], "enabled", true))) evs[i]["enabled"] = !jbool(evs[i], "enabled", true);
    if (ImGui::MenuItem("Copiar", "Ctrl+C")) { int e = isEv ? blockEnd(evs, i) : i + 1; clipEvents = json(std::vector<json>(evs.begin() + i, evs.begin() + e)); }
    if (ImGui::MenuItem("Cortar", "Ctrl+X")) { int e = isEv ? blockEnd(evs, i) : i + 1; clipEvents = json(std::vector<json>(evs.begin() + i, evs.begin() + e)); g_post.push_back([this, &proj, i]() { deleteEvent(proj, i); }); }
    if (ImGui::MenuItem("Pegar debajo", "Ctrl+V", false, !clipEvents.empty())) {
        g_post.push_back([this, &proj, i]() {
            json &ev2 = events(proj);
            int at = !ev2[i].contains("kind") ? blockEnd(ev2, i) : i + 1;
            for (auto c : clipEvents) { c["id"] = makeUid("ev"); ev2.insert(ev2.begin() + at++, c); }
            proj.dirty = true;
        });
    }
    if (ImGui::MenuItem("Duplicar", "Ctrl+D")) {
        g_post.push_back([this, &proj, i]() {
            json &ev2 = events(proj);
            int e = !ev2[i].contains("kind") ? blockEnd(ev2, i) : i + 1;
            std::vector<json> block(ev2.begin() + i, ev2.begin() + e);
            for (auto &c : block) c["id"] = makeUid("ev");
            insertRange(ev2, e, block);
            proj.dirty = true;
        });
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Eliminar", "Supr")) g_post.push_back([this, &proj, i]() { deleteEvent(proj, i); });
}

// trabajar juntos: el evento que cada persona tiene seleccionado (borde de su color) y su cursor; y lo propio para ellos
void EventsEditor::drawPresence(json &evs, const std::map<std::string, ImRect> &rects) {
    if (collab::role() == collab::Role::None) return;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const std::string doc = "events:" + sceneId;
    for (auto &p : collab::peers()) {
        if (p.doc != doc) continue;
        for (auto &id : p.sel) {
            auto it = rects.find(id);
            if (it == rects.end()) continue;
            const ImRect &r = it->second;
            dl->AddRect(r.Min, ImVec2(r.Max.x, r.Max.y - 2), p.color, 6, 0, 2.0f);
            ImVec2 ts = ImGui::CalcTextSize(p.name.c_str());
            dl->AddRectFilled(ImVec2(r.Max.x - ts.x - 14, r.Min.y), ImVec2(r.Max.x, r.Min.y + ts.y + 4), p.color, 4);
            dl->AddText(ImVec2(r.Max.x - ts.x - 7, r.Min.y + 2), IM_COL32(20, 20, 26, 255), p.name.c_str());
        }
        std::string at = p.extra.is_object() ? jstr(p.extra, "at") : "";
        auto it = rects.find(at);
        if (!p.cursor || it == rects.end()) continue;
        ImVec2 c(it->second.Min.x + p.x * it->second.GetWidth(), it->second.Min.y + p.y);
        ImVec2 tri[3] = { c, ImVec2(c.x + 3, c.y + 17), ImVec2(c.x + 13, c.y + 12) };
        dl->AddTriangleFilled(tri[0], tri[1], tri[2], p.color);
        dl->AddTriangle(tri[0], tri[1], tri[2], IM_COL32(20, 20, 26, 255), 1.2f);
        ImVec2 ts = ImGui::CalcTextSize(p.name.c_str()), lp(c.x + 12, c.y + 16);
        dl->AddRectFilled(lp, ImVec2(lp.x + ts.x + 10, lp.y + ts.y + 4), p.color, 4);
        dl->AddText(ImVec2(lp.x + 5, lp.y + 2), IM_COL32(20, 20, 26, 255), p.name.c_str());
    }
    // lo propio: el evento bajo el raton (y donde dentro de el) y el seleccionado
    ImVec2 m = ImGui::GetIO().MousePos;
    std::string at;
    float fx = 0, fy = 0;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        for (auto &kv : rects)
            if (kv.second.Contains(m)) { at = kv.first; fx = (m.x - kv.second.Min.x) / std::max(1.0f, kv.second.GetWidth()); fy = m.y - kv.second.Min.y; break; }
    std::vector<std::string> mine;
    if (sel >= 0 && sel < (int)evs.size()) mine.push_back(jstr(evs[sel], "id"));
    if (!at.empty() || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))   // solo el editor que se esta usando
        collab::setPresence(doc, fx, fy, !at.empty(), mine, at.empty() ? json() : json{{"at", at}});
}

void EventsEditor::drawEvent(Project &proj, json &evs, int i) {
    json &ev = evs[i];
    std::string kindE = jstr(ev, "kind");
    float availW = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(jstr(ev, "id", std::to_string(i)).c_str());
    bool inFolder = ev.contains("folder") && ev["folder"].is_string() && !ev["folder"].get<std::string>().empty();
    float indent = (float)jnum(ev, "level", 0) * 24 + (inFolder && kindE != "folder" ? 14 : 0);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    p0.x += indent;
    float W = availW - indent;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (kindE == "folder") {
        ImGui::SetCursorScreenPos(p0);
        float h = ImGui::GetFrameHeight() + 10;
        dl->AddRectFilledMultiColor(p0, ImVec2(p0.x + W, p0.y + h), IM_COL32(75, 54, 153, 255), IM_COL32(58, 44, 112, 255), IM_COL32(58, 44, 112, 255), IM_COL32(75, 54, 153, 255));
        if (sel == i) dl->AddRect(p0, ImVec2(p0.x + W, p0.y + h), IM_COL32(187, 168, 255, 255), 0.0f, 0, 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 6, p0.y + 5));
        bool col = jbool(ev, "collapsed", false);
        if (ui::iconButton("fold", col ? Icon::Next : Icon::Down, false, col ? "Desplegar" : "Plegar", ImGui::GetFrameHeight())) ev["collapsed"] = !col;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(W * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0.18f));
        editText("##fname", ev, "name");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        int n = 0; std::string fid = jstr(ev, "id");
        for (auto &e : evs) if (jstr(e, "folder") == fid) n++;
        ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%d dentro", n);
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 4));
        ImGui::Dummy(ImVec2(W, 0));
        ImRect r(p0, ImVec2(p0.x + W, p0.y + h));
        if (ImGui::IsMouseHoveringRect(r.Min, r.Max) && !ImGui::IsAnyItemHovered()) {
            if (ImGui::IsMouseClicked(0)) sel = i;
            if (ImGui::IsMouseClicked(1)) { sel = i; ImGui::OpenPopup("##evctx"); }
        }
        if (ImGui::BeginDragDropTargetCustom(r, ImGui::GetID("##fdrop"))) {
            if (const ImGuiPayload *pd = ImGui::AcceptDragDropPayload("S2D_EV")) {
                int src = *(const int *)pd->Data;
                g_post.push_back([this, &proj, src, fid]() {
                    json &e2 = events(proj);
                    if (src < 0 || src >= (int)e2.size() || e2[src].contains("kind")) return;
                    int e = blockEnd(e2, src);
                    for (int k = src; k < e; k++) e2[k]["folder"] = fid;
                    proj.dirty = true;
                });
            }
            ImGui::EndDragDropTarget();
        }
    } else if (kindE == "comment") {
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, p0.y + 6));
        std::string txt = jstr(ev, "text");
        int lines = 1 + (int)std::count(txt.begin(), txt.end(), '\n');
        float h = std::max(2, lines) * ImGui::GetTextLineHeight() + 12;
        dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + h + 12), IM_COL32(58, 53, 36, 255), 6);
        dl->AddRectFilled(p0, ImVec2(p0.x + 4, p0.y + h + 12), IM_COL32(243, 198, 77, 255), 2);
        if (sel == i) dl->AddRect(p0, ImVec2(p0.x + W, p0.y + h + 12), IM_COL32(243, 198, 77, 255), 6, 0, 1.5f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(246 / 255.f, 234 / 255.f, 176 / 255.f, 1));
        std::vector<char> buf(std::max<size_t>(2048, txt.size() + 1024));
        memcpy(buf.data(), txt.c_str(), txt.size() + 1);
        ImVec2 tp = ImGui::GetCursorScreenPos();
        if (ImGui::InputTextMultiline("##ctext", buf.data(), buf.size(), ImVec2(W - 16, h), 0)) ev["text"] = std::string(buf.data());
        if (txt.empty() && !ImGui::IsItemActive()) dl->AddText(ImVec2(tp.x + 6, tp.y + 4), IM_COL32(246, 234, 176, 110), "Escribe un comentario...");
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemClicked()) sel = i;
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 16));
        ImGui::Dummy(ImVec2(W, 0));
        ImRect r(p0, ImVec2(p0.x + W, p0.y + h + 12));
        if (ImGui::IsMouseHoveringRect(r.Min, r.Max) && ImGui::IsMouseClicked(1)) { sel = i; ImGui::OpenPopup("##evctx"); }
    } else {
        // evento: canal 0 = fondo, canal 1 = contenido
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        const float gutter = 14;
        float x0 = p0.x + gutter, cw = W - gutter;
        float y = p0.y;
        std::string type = jstr(ev, "type");
        float headerH = 0;
        if (!type.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(x0 + 8, y + 5));
            ImGui::AlignTextToFramePadding();
            if (type == "repeat") {
                ImGui::TextUnformatted("Repetir"); ImGui::SameLine(); ImGui::SetNextItemWidth(110); editText("##count", ev, "count");
                std::string err = checkExpression(proj, jstr(ev, "count", "1"));
                ImGui::SameLine(); ImGui::TextUnformatted("veces, con el contador"); ImGui::SameLine(); ImGui::SetNextItemWidth(70); editText("##var", ev, "var");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Variable opcional que vale 0, 1, 2... en cada vuelta (local si el bucle está dentro de una función)");
                if (!err.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, .56f, .55f, 1), "(%s)", err.c_str()); }
            } else if (type == "function") {
                ImGui::TextColored(ImVec4(0.62f, 0.78f, 1, 1), "Función"); ImGui::SameLine(); ImGui::SetNextItemWidth(150); editText("##fname", ev, "name");
                ImGui::SameLine(); ImGui::TextUnformatted("("); ImGui::SameLine(); ImGui::SetNextItemWidth(170); editText("##fparams", ev, "params");
                ImGui::SameLine(); ImGui::TextUnformatted(")");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Los parámetros, separados por comas, son variables locales de la función");
                ImGui::SameLine(); ImGui::TextDisabled("se ejecuta al llamarla: Fn.%s(...) o la acción \"Llamar a una función\"", jstr(ev, "name").c_str());
            } else if (type == "forEach") {
                ImGui::TextUnformatted("Para cada instancia de"); ImGui::SameLine(); ImGui::SetNextItemWidth(200); json objs = sceneObjects(proj, sceneId); comboById("##fe", ev, "object", objs);
            } else if (type == "while") {
                ImGui::TextUnformatted("Mientras se cumplan estas condiciones:");
            }
            headerH = ImGui::GetFrameHeight() + 10;
            y += headerH;
            if (type == "while") {
                if (!ev.contains("whileConds") || !ev["whileConds"].is_array()) ev["whileConds"] = json::array();
                ImGui::SetCursorScreenPos(ImVec2(x0 + 4, y + 4));
                ImGui::BeginGroup();
                ImGui::PushItemWidth(cw * 0.42f - 8);
                drawInstrColumn(proj, ev, i, "cond", "w", ev["whileConds"], cw * 0.42f - 8);
                ImGui::PopItemWidth();
                ImGui::EndGroup();
                float wh = ImGui::GetItemRectMax().y - y + 6;
                dl->ChannelsSetCurrent(0);
                dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + cw * 0.42f, y + wh), IM_COL32(52, 48, 74, 255));
                dl->ChannelsSetCurrent(1);
                y += wh;
                ImGui::SetCursorScreenPos(ImVec2(x0 + 8, y + 3));
                ImGui::TextDisabled("repetir estas condiciones y acciones:");
                y += ImGui::GetTextLineHeight() + 6;
                headerH = y - p0.y;
            }
        }
        float wc = cw * 0.42f;
        if (!ev.contains("conditions") || !ev["conditions"].is_array()) ev["conditions"] = json::array();
        if (!ev.contains("actions") || !ev["actions"].is_array()) ev["actions"] = json::array();
        ImGui::SetCursorScreenPos(ImVec2(x0 + 4, y + 4));
        ImGui::BeginGroup();
        drawInstrColumn(proj, ev, i, "cond", "c", ev["conditions"], wc - 8);
        ImGui::EndGroup();
        float ch = ImGui::GetItemRectMax().y - y;
        ImGui::SetCursorScreenPos(ImVec2(x0 + wc + 6, y + 4));
        ImGui::BeginGroup();
        drawInstrColumn(proj, ev, i, "act", "a", ev["actions"], cw - wc - 12);
        ImGui::EndGroup();
        float ah = ImGui::GetItemRectMax().y - y;
        float bodyH = std::max(ch, ah) + 12;
        float H = headerH + bodyH;
        ImVec2 p1(p0.x + W, p0.y + H);
        dl->ChannelsSetCurrent(0);
        bool enabled = jbool(ev, "enabled", true);
        dl->AddRectFilled(p0, p1, IM_COL32(39, 39, 49, 255), 6);
        if (headerH > 0) dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(p1.x, p0.y + std::min(headerH, ImGui::GetFrameHeight() + 10)), IM_COL32(47, 42, 69, 255));
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + wc, p1.y), IM_COL32(48, 48, 60, 255));
        dl->AddLine(ImVec2(x0 + wc, y), ImVec2(x0 + wc, p1.y), IM_COL32(58, 58, 71, 255));
        bool gutterHov = ImGui::IsMouseHoveringRect(p0, ImVec2(x0, p1.y));
        dl->AddRectFilled(p0, ImVec2(x0, p1.y), sel == i ? ui::kAccent : gutterHov ? IM_COL32(75, 54, 153, 255) : IM_COL32(52, 52, 64, 255), 6, ImDrawFlags_RoundCornersLeft);
        for (float gy = p0.y + 6; gy < p1.y - 4; gy += 5) dl->AddLine(ImVec2(p0.x + 4, gy), ImVec2(x0 - 4, gy), IM_COL32(255, 255, 255, 30));
        dl->AddRect(p0, p1, sel == i ? ui::kAccent : IM_COL32(58, 58, 71, 255), 6, 0, sel == i ? 2.0f : 1.0f);
        if (!enabled) dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 26, 120), 6);
        dl->ChannelsMerge();
        // asa para arrastrar el evento (y su bloque de subeventos)
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##grip", ImVec2(gutter, H));
        if (ImGui::IsItemClicked(0)) sel = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Arrastrar para mover - clic derecho: opciones");
        if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("S2D_EV", &i, sizeof i); ImGui::TextUnformatted("Mover evento"); ImGui::EndDragDropSource(); }
        if (ImGui::IsItemClicked(1)) { sel = i; ImGui::OpenPopup("##evctx"); }
        ImRect r(p0, p1);
        if (ImGui::IsMouseHoveringRect(r.Min, r.Max) && !ImGui::IsAnyItemHovered()) {
            if (ImGui::IsMouseClicked(0)) { sel = i; selEv = -1; }
            if (ImGui::IsMouseClicked(1)) { sel = i; ImGui::OpenPopup("##evctx"); }
        }
        ImGui::SetCursorScreenPos(ImVec2(p0.x - indent, p1.y + 6));
        ImGui::Dummy(ImVec2(availW, 0));
    }
    // soltar un evento antes o despues de este
    {
        ImVec2 pmin = p0, pmax(p0.x + W, ImGui::GetCursorScreenPos().y - 4);
        if (ImGui::BeginDragDropTargetCustom(ImRect(pmin, pmax), ImGui::GetID("##evdrop"))) {
            if (const ImGuiPayload *pd = ImGui::AcceptDragDropPayload("S2D_EV", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                bool before = ImGui::GetIO().MousePos.y < (pmin.y + pmax.y) / 2;
                float ly = before ? pmin.y - 3 : pmax.y + 1;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(pmin.x, ly), ImVec2(pmax.x, ly), ui::kAccent, 3);
                if (pd->IsDelivery()) {
                    int src = *(const int *)pd->Data, dst = i;
                    g_post.push_back([this, &proj, src, dst, before]() {
                        json &e2 = events(proj);
                        if (src == dst || src < 0 || src >= (int)e2.size()) return;
                        int se = e2[src].contains("kind") ? src + 1 : blockEnd(e2, src);
                        if (dst >= src && dst < se) return;
                        std::vector<json> block(e2.begin() + src, e2.begin() + se);
                        int lvl0 = (int)jnum(block[0], "level", 0), dl2 = e2[dst].contains("kind") ? 0 : (int)jnum(e2[dst], "level", 0);
                        json dfold = e2[dst].contains("folder") ? e2[dst]["folder"] : json();
                        if (jstr(e2[dst], "kind") == "folder") dfold = json();
                        for (auto &b : block) {
                            if (!b.contains("kind")) b["level"] = std::max(0, (int)jnum(b, "level", 0) - lvl0 + dl2);
                            if (dfold.is_null()) b.erase("folder"); else b["folder"] = dfold;
                        }
                        e2.erase(e2.begin() + src, e2.begin() + se);
                        int d = dst > src ? dst - (se - src) : dst;
                        int at = before ? d : (e2[d].contains("kind") ? d + 1 : blockEnd(e2, d));
                        insertRange(e2, at, block);
                        sel = at; proj.dirty = true;
                    });
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    if (ImGui::BeginPopup("##evctx")) { eventContext(proj, evs, i); ImGui::EndPopup(); }
    ImGui::PopID();
}

void EventsEditor::drawToolbar(Project &proj) {
    bool addHit = ui::primaryButton("+ Añadir un evento");
    school::anchor("events.add");
    if (addHit) addEvent(proj, "", false);
    ImGui::SameLine();
    bool canSub = sel >= 0 && sel < (int)events(proj).size() && !events(proj)[sel].contains("kind");
    ImGui::BeginDisabled(!canSub);
    if (ImGui::Button("Subevento")) addEvent(proj, "", true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Añade un subevento al evento seleccionado: solo se ejecuta si se cumplen las condiciones del padre");
    ImGui::SameLine();
    if (ImGui::Button("Bucle")) ImGui::OpenPopup("##loops");
    if (ImGui::BeginPopup("##loops")) {
        if (ImGui::MenuItem("Repetir N veces")) addEvent(proj, "repeat", false);
        if (ImGui::MenuItem("Para cada objeto")) addEvent(proj, "forEach", false);
        if (ImGui::MenuItem("Mientras")) addEvent(proj, "while", false);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Función")) addEvent(proj, "function", false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Un bloque de eventos con nombre y parámetros que se ejecuta al llamarlo (puede devolver un valor y llamarse a sí mismo)");
    ImGui::SameLine();
    if (ImGui::Button("Comentario")) addComment(proj);
    ImGui::SameLine();
    if (ImGui::Button("Grupo")) addGroup(proj);
    ImGui::SameLine(0, 16);
    if (ui::iconButton("foldall", Icon::Down, false, "Plegar todos los grupos", 26)) for (auto &e : events(proj)) if (jstr(e, "kind") == "folder") e["collapsed"] = true;
    ImGui::SameLine();
    if (ui::iconButton("unfold", Icon::Up, false, "Desplegar todos los grupos", 26)) for (auto &e : events(proj)) if (jstr(e, "kind") == "folder") e["collapsed"] = false;
    ImGui::SameLine();
    if (ui::iconButton("undo", Icon::Undo, false, "Deshacer (Ctrl+Z)", 26)) undo(proj);
    ImGui::SameLine();
    if (ui::iconButton("redo", Icon::Redo, false, "Rehacer (Ctrl+Y)", 26)) redo(proj);
    ImGui::SameLine(0, 16);
    ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##search", "Buscar en los eventos", search, sizeof search);
}

void EventsEditor::shortcuts(Project &proj) {
    ui::noteFocus("events", sceneId);
    if (int h = ui::takeHistoryRequest()) { if (h > 0) redo(proj); else undo(proj); sel = -1; selEv = -1; }
    if (dlg.open || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
    ImGuiIO &io = ImGui::GetIO();
    json &evs = events(proj);
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) redo(proj); else undo(proj); sel = -1; selEv = -1; }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) { redo(proj); sel = -1; selEv = -1; }
        if (sel >= 0 && sel < (int)evs.size()) {
            int e = evs[sel].contains("kind") ? sel + 1 : blockEnd(evs, sel);
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) clipEvents = json(std::vector<json>(evs.begin() + sel, evs.begin() + e));
            if (ImGui::IsKeyPressed(ImGuiKey_X, false)) { clipEvents = json(std::vector<json>(evs.begin() + sel, evs.begin() + e)); deleteEvent(proj, sel); }
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) { std::vector<json> b(evs.begin() + sel, evs.begin() + e); for (auto &c : b) c["id"] = makeUid("ev"); insertRange(evs, e, b); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false) && !clipEvents.empty()) {
            int at = sel >= 0 && sel < (int)evs.size() ? (evs[sel].contains("kind") ? sel + 1 : blockEnd(evs, sel)) : (int)evs.size();
            for (auto c : clipEvents) { c["id"] = makeUid("ev"); evs.insert(evs.begin() + at++, c); }
        }
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        json *l = nullptr; int idx = 0;
        if (selEv >= 0 && selEv < (int)evs.size() && instrAt(evs[selEv], selPath, &l, &idx)) { l->erase(l->begin() + idx); selEv = -1; }
        else if (sel >= 0) deleteEvent(proj, sel);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { sel = -1; selEv = -1; }
}

void EventsEditor::draw(Project &proj) {
    g_sheetScene = sceneId;
    json &evs = events(proj);
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title(proj).c_str(), &open)) { ImGui::End(); drawDialog(proj); hist.track(evs); return; }
    json before = evs;
    shortcuts(proj);
    drawToolbar(proj);
    ImGui::Separator();
    ImGui::BeginChild("##sheet", ImVec2(0, 0), ImGuiChildFlags_None);
    if (evs.empty()) {
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::TextDisabled("Todavía no hay eventos.");
        ImGui::TextWrapped("Los eventos le dicen al juego que hacer: si se cumplen las condiciones (izquierda), se ejecutan las acciones (derecha).");
        if (ui::primaryButton("Añadir el primer evento")) addEvent(proj, "", false);
    }
    std::map<std::string, bool> collapsed;
    for (auto &e : evs) if (jstr(e, "kind") == "folder") collapsed[jstr(e, "id")] = jbool(e, "collapsed", false);
    // donde quedo cada evento en pantalla: los cursores de los demas van anclados al evento (no a la pantalla, que es
    // distinta en cada equipo), en la misma fraccion del ancho y a la misma altura dentro de el
    std::map<std::string, ImRect> rects;
    float sheetX = ImGui::GetCursorScreenPos().x, sheetW = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < (int)evs.size(); i++) {
        std::string f = jstr(evs[i], "folder");
        if (!f.empty() && collapsed[f] && jstr(evs[i], "kind") != "folder") continue;
        if (search[0] && jstr(evs[i], "kind") != "folder" && !matchesSearch(proj, evs[i])) continue;
        float y0 = ImGui::GetCursorScreenPos().y;
        drawEvent(proj, evs, i);
        rects[jstr(evs[i], "id")] = ImRect(sheetX, y0, sheetX + sheetW, ImGui::GetCursorScreenPos().y);
    }
    drawPresence(evs, rects);
    ImGui::Dummy(ImVec2(0, 80));
    if (scrollToEnd) { ImGui::SetScrollHereY(1.0f); scrollToEnd = false; }
    ImGui::EndChild();
    ImGui::End();
    drawDialog(proj);
    for (auto &fn : g_post) fn();
    g_post.clear();
    json &after = events(proj);
    if (after != before) proj.dirty = true;
    hist.track(after);
}
