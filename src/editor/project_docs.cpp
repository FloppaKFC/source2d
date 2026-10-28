#include "project_docs.hpp"
#include "code_editor.hpp"
#include "thumbnails.hpp"
#include "raylib.h"
#include "rlgl.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "events_editor.hpp"
#include "presets.h"
#include "quickjs.h"
#include "raylib.h"

namespace fs = std::filesystem;

static void row(const char *l, float w = 170) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::SameLine(w); ImGui::SetNextItemWidth(-1); }
static json &arr(Project &proj, const char *key) {
    if (!proj.doc.contains(key) || !proj.doc[key].is_array()) proj.doc[key] = json::array();
    return proj.doc[key];
}

/* ---------------- extensiones (QuickJS) ---------------- */
static JSRuntime *g_rt = nullptr;
static JSContext *g_ctx = nullptr;

// el contexto no tiene acceso a archivos, pero un bucle sin fin colgaria el editor: se corta a los 2 s
static double g_evalUntil = 0;
static int stopSlowEval(JSContext *, void *) { return GetTime() > g_evalUntil; }

// ejecuta el codigo de una extension y devuelve sus definiciones (sin funciones) como JSON
static bool evalExtension(const std::string &code, json &out, std::string &err) {
    if (!g_rt) {
        g_rt = JS_NewRuntime(); g_ctx = JS_NewContext(g_rt); JS_SetMaxStackSize(g_rt, 1 << 20);
        JS_SetMemoryLimit(g_rt, 256u << 20);
        JS_SetInterruptHandler(g_rt, stopSlowEval, nullptr);
    }
    g_evalUntil = GetTime() + 2.0;
    std::string src = "(function(){ var r = (function(api){\n" + code + "\n})({}); return JSON.stringify(r === undefined ? null : r); })()";
    JSValue v = JS_Eval(g_ctx, src.c_str(), src.size(), "<extension>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JSValue ex = JS_GetException(g_ctx);
        const char *m = JS_ToCString(g_ctx, ex);
        err = m ? m : "error";
        if (m) JS_FreeCString(g_ctx, m);
        JS_FreeValue(g_ctx, ex);
        return false;
    }
    const char *s = JS_ToCString(g_ctx, v);
    std::string str = s ? s : "null";
    if (s) JS_FreeCString(g_ctx, s);
    JS_FreeValue(g_ctx, v);
    try { out = json::parse(str); } catch (...) { err = "no devolvió un objeto"; return false; }
    if (!out.is_object()) { err = "Termina con: return { actions: { ... }, conditions: { ... } };"; return false; }
    return true;
}

std::map<std::string, std::string> refreshExtensionDefs(Project &proj) {
    static size_t lastHash = 0;
    static std::map<std::string, std::string> status;
    std::string all;
    for (auto &x : arr(proj, "extensions")) all += jstr(x, "id") + jstr(x, "name") + jstr(x, "code");
    for (auto &x : arr(proj, "plugins")) all += jstr(x, "id") + jstr(x, "name") + jstr(x, "code") + (jbool(x, "enabled", true) ? "1" : "0");
    size_t h = std::hash<std::string>{}(all);
    if (h == lastHash) return status;
    lastHash = h;
    status.clear();
    InstrDefs &D = InstrDefs::get();
    D.ext = {{"cond", json::object()}, {"act", json::object()}};
    auto addList = [&](json &list, bool plugin) {
        for (auto &x : list) {
            std::string id = jstr(x, "id");
            if (plugin && !jbool(x, "enabled", true)) { status[id] = "Desactivado"; continue; }
            json out; std::string err;
            if (!evalExtension(jstr(x, "code"), out, err)) { status[id] = "Error: " + err; continue; }
            int na = 0, nc = 0;
            std::string cat = "Extensión: " + jstr(x, "name", "sin nombre");
            for (const char *k : {"actions", "conditions"}) {
                if (!out.contains(k) || !out[k].is_object()) continue;
                for (auto it = out[k].begin(); it != out[k].end(); ++it) {
                    json params = json::array();
                    if (it->contains("params") && (*it)["params"].is_array())
                        for (auto &p : (*it)["params"]) {
                            json q = {{"k", jstr(p, "k")}, {"l", jstr(p, "l")}, {"t", jstr(p, "t", "text")}, {"d", p.contains("d") ? p["d"] : json("")}};
                            if (p.contains("options")) q["options"] = p["options"];
                            params.push_back(q);
                        }
                    json df = {{"cat", cat}, {"label", jstr(*it, "label", it.key())}, {"params", params}};
                    D.ext[std::string(k) == "actions" ? "act" : "cond"]["ext:" + id + ":" + it.key()] = df;
                    (std::string(k) == "actions" ? na : nc)++;
                }
            }
            status[id] = "Correcto: " + std::to_string(na) + " acción(es) y " + std::to_string(nc) + " condición(es).";
        }
    };
    addList(arr(proj, "extensions"), false);
    addList(arr(proj, "plugins"), true);
    return status;
}

std::string addExtension(Project &proj, bool plugin, bool estilo) {
    json &l = arr(proj, plugin ? "plugins" : "extensions");
    std::string id = estilo ? "ext_estilo_gd" : makeUid(plugin ? "pl" : "ext");
    if (findById(l, id)) return id;
    std::string code = estilo ? std::string((const char *)preset_estilo, preset_estilo_len) : std::string((const char *)preset_extension, preset_extension_len);
    json x = {{"id", id}, {"name", estilo ? "Plugin de estilo (GDevelop)" : plugin ? "Plugin " + std::to_string(l.size() + 1) : "Extensión " + std::to_string(l.size() + 1)}, {"code", code}};
    if (plugin) x["enabled"] = true;
    l.push_back(x);
    proj.dirty = true;
    return id;
}

// extension oficial Steam (presets/steam.js): una sola por proyecto, con id fijo
std::string addSteamExtension(Project &proj) {
    json &l = arr(proj, "extensions");
    std::string id = "ext_steam";
    if (findById(l, id)) return id;
    l.push_back({{"id", id}, {"name", "Steam"}, {"code", std::string((const char *)preset_steam, preset_steam_len)}});
    proj.dirty = true;
    return id;
}

std::string addRainShader(Project &proj) {
    json &l = arr(proj, "shaders");
    if (findById(l, "sh_rain")) return "sh_rain";
    l.push_back({{"id", "sh_rain"}, {"name", "Lluvia"}, {"code", std::string((const char *)preset_lluvia, preset_lluvia_len)}});
    proj.dirty = true;
    return "sh_rain";
}

/* ---------------- ajustes del proyecto ---------------- */
void SettingsEditor::draw(Project &proj) {
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(800, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Ajustes del proyecto###settings", &open)) { ImGui::End(); return; }
    ui::noteFocus("settings");
    json before = proj.doc["settings"];
    json globalsBefore = arr(proj, "globals");
    std::string nameBefore = proj.name();
    json &st = proj.doc["settings"].is_object() ? proj.doc["settings"] : (proj.doc["settings"] = json::object());
    ImGui::SeparatorText("Juego");
    row("Nombre del juego"); editText("##pname", proj.doc, "name");
    ImGui::TextDisabled("Es también el título de la ventana y el nombre propuesto al exportar.");
    ImGui::SeparatorText("Resolución");
    row("Ancho"); editNum("##w", st, "width", 960, 1, "%.0f px", 160, 4096);
    row("Alto"); editNum("##h", st, "height", 540, 1, "%.0f px", 120, 4096);
    ImGui::Dummy(ImVec2(162, 0)); ImGui::SameLine();
    const int presets[][2] = {{320, 180}, {640, 360}, {960, 540}, {1280, 720}, {1920, 1080}};
    for (auto &p : presets) {
        char b[24]; snprintf(b, sizeof b, "%dx%d", p[0], p[1]);
        if (ImGui::SmallButton(b)) { st["width"] = p[0]; st["height"] = p[1]; }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::TextDisabled("El juego se escala a la ventana manteniendo la proporción (con bandas negras si hace falta).");
    ImGui::SeparatorText("Pantalla");
    editBool("Permitir pantalla completa con F11", st, "fullscreen", true);
    editBool("Abrir directamente a pantalla completa", st, "autofullscreen", false);
    editBool("Escala entera (píxel perfecto, con bandas)", st, "pixelPerfect", false);
    ImGui::SeparatorText("Tiempo");
    editBool("Paso fijo de 1/60 s (la física igual en cualquier equipo; el dibujo se suaviza)", st, "fixedStep", false);

    ImGui::SeparatorText("Variables globales");
    ImGui::TextDisabled("Se conservan entre escenas. En los eventos: \"Variable global\" y en textos {g:nombre}.");
    json &gl = arr(proj, "globals");
    int del = -1;
    static const std::vector<std::pair<std::string, std::string>> types = {{"normal", "Número"}, {"text", "Texto"}, {"bool", "Booleano"}, {"array", "Lista"}, {"parent", "Estructura"}};
    if (ImGui::BeginTable("##gl", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Nombre", 0, 1.0f); ImGui::TableSetupColumn("Tipo", 0, 0.7f); ImGui::TableSetupColumn("Valor", 0, 1.6f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)gl.size(); i++) {
            json &g = gl[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); editText("##n", g, "name");
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); comboOptions("##t", g, "type", types, "normal");
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
            std::string tp = jstr(g, "type", "normal");
            if (tp == "bool") { bool b = jbool(g, "value", false); if (ImGui::Checkbox("##b", &b)) g["value"] = b; }
            else if (tp == "array") {
                std::string joined;
                if (g.contains("value") && g["value"].is_array()) for (auto &v : g["value"]) joined += (joined.empty() ? "" : ", ") + (v.is_string() ? v.get<std::string>() : v.dump());
                char buf[512]; snprintf(buf, sizeof buf, "%s", joined.c_str());
                if (ImGui::InputTextWithHint("##a", "valores separados por comas", buf, sizeof buf)) {
                    json a = json::array(); std::string cur;
                    for (char *c = buf;; c++) {
                        if (*c == ',' || !*c) { size_t s0 = cur.find_first_not_of(' '), s1 = cur.find_last_not_of(' '); if (s0 != std::string::npos) a.push_back(cur.substr(s0, s1 - s0 + 1)); cur.clear(); if (!*c) break; }
                        else cur += *c;
                    }
                    g["value"] = a;
                }
            } else if (tp == "parent") {
                if (!g.contains("children") || !g["children"].is_array()) g["children"] = json::array();
                ImGui::TextDisabled("%d campo(s)", (int)g["children"].size());
                ImGui::SameLine(); if (ImGui::SmallButton("+ campo")) g["children"].push_back({{"name", "campo" + std::to_string(g["children"].size() + 1)}, {"value", 0}});
                int cd = -1;
                for (int k = 0; k < (int)g["children"].size(); k++) {
                    ImGui::PushID(k);
                    ImGui::SetNextItemWidth(90); editText("##cn", g["children"][k], "name"); ImGui::SameLine();
                    ImGui::SetNextItemWidth(-40); editText("##cv", g["children"][k], "value"); ImGui::SameLine();
                    if (ImGui::SmallButton("x")) cd = k;
                    ImGui::PopID();
                }
                if (cd >= 0) g["children"].erase(g["children"].begin() + cd);
            } else {
                std::string v = jstr(g, "value");
                char buf[256]; snprintf(buf, sizeof buf, "%s", v.c_str());
                if (ImGui::InputText("##v", buf, sizeof buf)) {
                    char *e = nullptr; double d = strtod(buf, &e);
                    g["value"] = (tp == "normal" && buf[0] && e && *e == 0) ? (d == (long long)d ? json((long long)d) : json(d)) : json(std::string(buf));
                }
            }
            ImGui::TableNextColumn(); if (ImGui::SmallButton("Quitar")) del = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (del >= 0) gl.erase(gl.begin() + del);
    if (ui::primaryButton("+ Variable global")) gl.push_back({{"name", "var" + std::to_string(gl.size() + 1)}, {"type", "normal"}, {"value", 0}});

    // idiomas: tabla clave -> texto por idioma. En textos {t:clave}; en expresiones T("clave")
    ImGui::SeparatorText("Idiomas");
    ImGui::TextDisabled("En los textos: {t:clave}. Acción \"Cambiar el idioma del juego\".");
    if (!proj.doc.contains("i18n") || !proj.doc["i18n"].is_object()) proj.doc["i18n"] = {{"langs", json::array({"es"})}, {"strings", json::object()}};
    json &i18 = proj.doc["i18n"], i18Before = i18;
    if (!i18.contains("langs") || !i18["langs"].is_array() || i18["langs"].empty()) i18["langs"] = json::array({"es"});
    if (!i18.contains("strings") || !i18["strings"].is_object()) i18["strings"] = json::object();
    json &langs = i18["langs"], &strs = i18["strings"];
    int nl = (int)langs.size();
    static char newLang[16] = "en";
    ImGui::SetNextItemWidth(80); ImGui::InputText("##nl", newLang, sizeof newLang); ImGui::SameLine();
    if (ImGui::SmallButton("+ Idioma") && newLang[0] && std::find(langs.begin(), langs.end(), json(newLang)) == langs.end()) langs.push_back(newLang);
    if (ImGui::BeginTable("##i18n", nl + 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Clave", 0, 0.8f);
        for (auto &l : langs) ImGui::TableSetupColumn(l.get<std::string>().c_str(), 0, 1.4f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        std::string delKey, renFrom, renTo;
        for (auto it = strs.begin(); it != strs.end(); ++it) {
            ImGui::PushID(it.key().c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char kb[64]; snprintf(kb, sizeof kb, "%s", it.key().c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##k", kb, sizeof kb, ImGuiInputTextFlags_EnterReturnsTrue) && kb[0] && !strs.contains(kb)) { renFrom = it.key(); renTo = kb; }
            for (auto &l : langs) { ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); editText(("##" + l.get<std::string>()).c_str(), it.value(), l.get<std::string>().c_str()); }
            ImGui::TableNextColumn(); if (ImGui::SmallButton("Quitar")) delKey = it.key();
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!delKey.empty()) strs.erase(delKey);
        if (!renFrom.empty()) { strs[renTo] = strs[renFrom]; strs.erase(renFrom); }
    }
    if (ImGui::SmallButton("+ Texto")) { int k = (int)strs.size() + 1; std::string key; do key = "texto" + std::to_string(k++); while (strs.contains(key)); strs[key] = json::object(); }
    if (i18 != i18Before) proj.dirty = true;
    ImGui::End();
    if (proj.doc["settings"] != before || arr(proj, "globals") != globalsBefore || proj.name() != nameBefore) proj.dirty = true;
}

/* ---------------- shaders ---------------- */
/* vista previa del shader: la miniatura de la primera escena pasada por el shader, en vivo (u_time corre) */
static void shaderPreview(Project &proj, const std::string &code, bool ok, float w) {
    static RenderTexture2D rt{};
    static Shader sh{};
    static std::string shCode;
    static bool shOk = false;
    float h = w * kThumbH / kThumbW;
    ui::PixTexture *th = docThumb("shaderprev", proj.doc, proj.rev, false);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(16, 17, 22, 255), 6);
    if (!th) { dl->AddText(ImVec2(p.x + 10, p.y + 10), ui::kDim, "Preparando la escena..."); return; }
    if (ok && code != shCode) {
        if (shOk) UnloadShader(sh);
        shCode = code;
        sh = LoadShaderFromMemory(nullptr, glslForEditor(code).c_str());
        shOk = IsShaderValid(sh) && sh.id != rlGetShaderIdDefault();
    }
    if (!rt.id) rt = LoadRenderTexture(kThumbW, kThumbH);
    BeginTextureMode(rt);
    ClearBackground(BLANK);
    if (ok && shOk) {
        BeginShaderMode(sh);
        float t = (float)GetTime(), res[2] = {(float)kThumbW, (float)kThumbH}, cam[3] = {0, 0, 1}, amt = 1;
        int lt = GetShaderLocation(sh, "u_time"), lr = GetShaderLocation(sh, "u_res"), lc = GetShaderLocation(sh, "u_cam"), la = GetShaderLocation(sh, "u_amount");
        if (lt >= 0) SetShaderValue(sh, lt, &t, SHADER_UNIFORM_FLOAT);
        if (lr >= 0) SetShaderValue(sh, lr, res, SHADER_UNIFORM_VEC2);
        if (lc >= 0) SetShaderValue(sh, lc, cam, SHADER_UNIFORM_VEC3);
        if (la >= 0) SetShaderValue(sh, la, &amt, SHADER_UNIFORM_FLOAT);
    }
    DrawTexture(th->tex, 0, 0, WHITE);
    if (ok && shOk) EndShaderMode();
    EndTextureMode();
    dl->AddImage((ImTextureID)(intptr_t)rt.texture.id, p, ImVec2(p.x + w, p.y + h), ImVec2(0, 1), ImVec2(1, 0));
    if (!ok || !shOk) dl->AddText(ImVec2(p.x + 8, p.y + h - 22), IM_COL32(255, 120, 120, 255), "Sin shader: arregla los errores");
}
void ShadersEditor::draw(Project &proj) {
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(900, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Shaders###shaders", &open)) { ImGui::End(); return; }
    ui::noteFocus("settings");
    json &l = arr(proj, "shaders");
    json before = l;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##shl", ImVec2(230, avail.y));
    if (ui::primaryButton("+ Shader nuevo", ImVec2(-1, 0))) {
        std::string id = makeUid("sh");
        l.push_back({{"id", id}, {"name", "Shader " + std::to_string(l.size() + 1)},
                     {"code", "precision mediump float;\nvarying vec2 v_uv;\nuniform sampler2D u_tex;\nuniform float u_time;\nvoid main() {\n  vec4 c = texture2D(u_tex, v_uv);\n  gl_FragColor = vec4(1.0 - c.rgb, c.a);\n}\n"}});
        cur = id;
    }
    if (ImGui::Button("+ Plantilla: Lluvia", ImVec2(-1, 0))) cur = addRainShader(proj);
    ImGui::Separator();
    for (auto &s : l) {
        std::string id = jstr(s, "id");
        ImGui::PushID(id.c_str());
        if (ImGui::Selectable(jstr(s, "name", "Shader").c_str(), id == cur)) cur = id;
        ImGui::PopID();
    }
    if (l.empty()) ImGui::TextDisabled("Sin shaders.");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##shb", ImVec2(0, avail.y));
    if (json *s = findById(l, cur)) {
        row("Nombre", 90); editText("##n", *s, "name");
        ImGui::TextDisabled("GLSL ES 1.0: v_uv (coordenada), u_tex (imagen de la capa) y u_time (segundos).");
        ImGui::TextDisabled("Se aplica a una capa (Escena > Capas), al objeto (Efecto) o a toda la escena (Efectos de la escena).");
        // codigo a la izquierda y vista previa en vivo de la escena con el shader a la derecha
        float pw = std::min(384.0f, ImGui::GetContentRegionAvail().x * 0.42f), codeW = ImGui::GetContentRegionAvail().x - pw - 12;
        std::vector<CodeError> errs;
        ImGui::BeginGroup();
        codeField("##code", *s, "code", ImVec2(codeW, ImGui::GetContentRegionAvail().y - 40), CodeLang::GLSL, false, &errs);
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("Vista previa (primera escena)");
        shaderPreview(proj, jstr(*s, "code"), errs.empty(), pw);
        ImGui::EndGroup();
        if (ui::deleteButton("Eliminar shader", "el shader \"" + jstr(*s, "name", "sin nombre") + "\"")) {
            std::string id = cur;
            for (size_t i = 0; i < l.size(); i++) if (jstr(l[i], "id") == id) { l.erase(l.begin() + i); break; }
            // quien lo usaba (escenas, capas, objetos) queda sin shader, en vez de apuntar a uno que ya no existe
            auto unlink = [&](json &arr) { if (arr.is_array()) for (auto &x : arr) if (x.is_object() && jstr(x, "shaderId") == id) x.erase("shaderId"); };
            unlink(proj.doc["scenes"]); unlink(proj.doc["layers"]); unlink(proj.doc["objects"]);
            cur.clear();
        }
    } else ImGui::TextDisabled("Elige o crea un shader a la izquierda.");
    ImGui::EndChild();
    ImGui::End();
    if (l != before) proj.dirty = true;
}

/* ---------------- extensiones y plugins ---------------- */
void ExtensionsEditor::draw(Project &proj) {
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(950, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Extensiones###extensions", &open)) { ImGui::End(); return; }
    ui::noteFocus("settings");
    json &ex = arr(proj, "extensions"), &pl = arr(proj, "plugins");
    json b1 = ex, b2 = pl;
    auto status = refreshExtensionDefs(proj);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##exl", ImVec2(250, avail.y));
    ImGui::SeparatorText("Extensiones");
    if (ImGui::Button("+ Extensión nueva", ImVec2(-1, 0))) cur = addExtension(proj, false, false);
    if (ImGui::Button("+ Plugin de estilo", ImVec2(-1, 0))) cur = addExtension(proj, false, true);
    if (ui::primaryButton("+ Steam (oficial)", ImVec2(-1, 0))) cur = addSteamExtension(proj);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Logros, estadísticas, guardado en la nube, overlay y presencia de Steam.\n"
                                                  "Al exportar, pon libsteam_api.so / steam_api64.dll del SDK de Steamworks junto al juego.");
    for (auto &x : ex) { std::string id = jstr(x, "id"); ImGui::PushID(id.c_str()); if (ImGui::Selectable(jstr(x, "name").c_str(), id == cur)) cur = id; ImGui::PopID(); }
    ImGui::SeparatorText("Plugins");
    if (ImGui::Button("+ Plugin nuevo", ImVec2(-1, 0))) cur = addExtension(proj, true, false);
    for (auto &x : pl) { std::string id = jstr(x, "id"); ImGui::PushID(id.c_str()); if (ImGui::Selectable(jstr(x, "name").c_str(), id == cur)) cur = id; ImGui::PopID(); }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##exb", ImVec2(0, avail.y));
    json *x = findById(ex, cur);
    bool plugin = false;
    if (!x) { x = findById(pl, cur); plugin = x != nullptr; }
    if (x) {
        row("Nombre", 90); editText("##n", *x, "name");
        if (plugin) editBool("Activado", *x, "enabled", true);
        std::string stt = status.count(cur) ? status[cur] : "";
        bool ok = stt.rfind("Correcto", 0) == 0;
        ImGui::TextColored(ok ? ImVec4(0.25f, 0.76f, 0.5f, 1) : ImVec4(1, 0.56f, 0.55f, 1), "%s", stt.c_str());
        ImGui::TextDisabled("api.text(k), api.arg(k), api.inst, api.vars, api.log(msg). Sus acciones y condiciones aparecen en la hoja de eventos.");
        codeField("##code", *x, "code", ImVec2(-1, ImGui::GetContentRegionAvail().y - 40), CodeLang::JS, false);
        if (ui::deleteButton(plugin ? "Eliminar plugin" : "Eliminar extensión", std::string(plugin ? "el plugin" : "la extensión") + " \"" + jstr(*x, "name", "sin nombre") + "\"")) {
            json &l = plugin ? pl : ex;
            for (size_t i = 0; i < l.size(); i++) if (jstr(l[i], "id") == cur) { l.erase(l.begin() + i); break; }
            cur.clear();
        }
    } else ImGui::TextDisabled("Las extensiones añaden condiciones y acciones nuevas escritas en JavaScript.");
    ImGui::EndChild();
    ImGui::End();
    if (ex != b1 || pl != b2) proj.dirty = true;
}

/* ---------------- musica ---------------- */
static const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64Encode(const unsigned char *d, size_t n) {
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out += B64[v >> 18 & 63]; out += B64[v >> 12 & 63];
        out += i + 1 < n ? B64[v >> 6 & 63] : '='; out += i + 2 < n ? B64[v & 63] : '=';
    }
    return out;
}
std::vector<unsigned char> base64Decode(const std::string &s) {
    std::vector<unsigned char> out;
    size_t start = s.find(',');
    start = start == std::string::npos ? 0 : start + 1;
    unsigned acc = 0; int bits = -8;
    for (size_t i = start; i < s.size(); i++) {
        const char *p = strchr(B64, s[i]);
        if (!p || !s[i]) continue;
        acc = acc << 6 | (unsigned)(p - B64); bits += 6;
        if (bits >= 0) { out.push_back((unsigned char)(acc >> bits & 0xFF)); bits -= 8; }
    }
    return out;
}

std::string importMusic(Project &proj, const std::string &file, std::string &err) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { err = "No se pudo leer " + file; return ""; }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string ext = fs::path(file).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    std::string mime = ext == ".ogg" || ext == ".oga" ? "audio/ogg" : ext == ".wav" ? "audio/wav" : ext == ".flac" ? "audio/flac" : ext == ".qoa" ? "audio/qoa" : "audio/mpeg";
    std::string id = makeUid("m");
    arr(proj, "music").push_back({{"id", id}, {"name", fs::path(file).stem().string()}, {"mime", mime}, {"data", "data:" + mime + ";base64," + base64Encode(data.data(), data.size())}});
    proj.dirty = true;
    return id;
}

/* archivos iniciales: los de texto (json, txt, csv) se guardan tal cual; el resto en base64 */
bool addInitialFile(Project &proj, const std::string &file, std::string &err) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { err = "No se pudo leer " + file; return false; }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string name = fs::path(file).filename().string(), ext = fs::path(file).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    json &fl = arr(proj, "files");
    for (size_t i = 0; i < fl.size(); i++) if (jstr(fl[i], "name") == name) { fl.erase(fl.begin() + i); break; }   // reemplaza al del mismo nombre
    json f = {{"id", makeUid("fi")}, {"name", name}};
    if (ext == ".json" || ext == ".txt" || ext == ".csv" || ext == ".md" || ext == ".xml") f["text"] = data;
    else {
        std::string mime = ext == ".png" ? "image/png" : ext == ".jpg" || ext == ".jpeg" ? "image/jpeg" : ext == ".mp3" ? "audio/mpeg"
                         : ext == ".ogg" ? "audio/ogg" : ext == ".wav" ? "audio/wav" : "application/octet-stream";
        f["mime"] = mime;
        f["data"] = base64Encode((const unsigned char *)data.data(), data.size());
    }
    fl.push_back(f);
    proj.dirty = true;
    return true;
}

static Music g_music{};
static std::vector<unsigned char> g_musicBuf;
static std::string g_musicId;

void musicPreview(Project &proj, const std::string &id) {
    if (g_musicId.size()) { StopMusicStream(g_music); UnloadMusicStream(g_music); g_musicId.clear(); }
    if (id.empty()) return;
    if (!IsAudioDeviceReady()) InitAudioDevice();
    json *m = findById(arr(proj, "music"), id);
    if (!m) return;
    g_musicBuf = base64Decode(jstr(*m, "data"));
    std::string mime = jstr(*m, "mime"), ext = mime.find("ogg") != std::string::npos ? ".ogg" : mime.find("wav") != std::string::npos ? ".wav" : mime.find("flac") != std::string::npos ? ".flac" : mime.find("qoa") != std::string::npos ? ".qoa" : ".mp3";
    g_music = LoadMusicStreamFromMemory(ext.c_str(), g_musicBuf.data(), (int)g_musicBuf.size());
    if (!IsMusicValid(g_music)) return;
    PlayMusicStream(g_music);
    g_musicId = id;
}
std::string musicPlaying() { return g_musicId; }
void musicUpdate() {
    if (g_musicId.empty()) return;
    UpdateMusicStream(g_music);
    if (!IsMusicStreamPlaying(g_music)) { UnloadMusicStream(g_music); g_musicId.clear(); }
}
void musicShutdown() {
    if (g_musicId.size()) { StopMusicStream(g_music); UnloadMusicStream(g_music); g_musicId.clear(); }
    if (IsAudioDeviceReady()) CloseAudioDevice();
}

/* ---------------- importar imagenes ---------------- */
bool ImportImage::load(const std::string &file) {
    Image img = LoadImage(file.c_str());
    if (!img.data) { error = "No se pudo abrir la imagen (PNG, JPG, GIF, BMP, TGA o QOI)"; return false; }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    imgW = img.width; imgH = img.height;
    px.assign((Px *)img.data, (Px *)img.data + (size_t)imgW * imgH);
    UnloadImage(img);
    path = file; name = fs::path(file).stem().string(); error.clear();
    fw = imgW; fh = imgH;
    if (asTiles) { fw = fh = 16; }
    tex.update(px, imgW, imgH);
    open = true;
    return true;
}

static json pixelsOf(const std::vector<Px> &px, int W, int x0, int y0, int w, int h) {
    json out = json::array();
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) out.push_back(colorToString(px[(size_t)(y0 + y) * W + x0 + x]));
    return out;
}

std::string ImportImage::commit(Project &proj) {
    int w = std::clamp(fw, 1, imgW), h = std::clamp(fh, 1, imgH);
    int cols = std::max(1, imgW / w), rows = std::max(1, imgH / h);
    std::string id;
    if (asTiles) {
        json tiles = json::array();
        for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) tiles.push_back({{"pixels", pixelsOf(px, imgW, c * w, r * h, w, w)}, {"solid", false}});
        id = makeUid("ts");
        arr(proj, "tilesets").push_back({{"id", id}, {"name", name}, {"tileSize", w}, {"columns", cols}, {"rows", rows}, {"tiles", tiles}});
    } else {
        json frames = json::array();
        for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) frames.push_back({{"pixels", pixelsOf(px, imgW, c * w, r * h, w, h)}});
        id = makeUid("s");
        proj.sprites().push_back({{"id", id}, {"name", name}, {"width", w}, {"height", h}, {"frames", frames}});
    }
    proj.dirty = true; proj.rev++;
    open = false;
    return id;
}

void ImportImage::draw(Project &proj, const std::function<void(const std::string &, bool)> &done) {
    if (!open) return;
    ImGui::OpenPopup("Importar imagen");
    ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_Appearing);
    if (!ui::beginModal("Importar imagen", &open)) return;
    ImGui::Text("%s  (%d x %d px)", fs::path(path).filename().string().c_str(), imgW, imgH);
    char nb[96]; snprintf(nb, sizeof nb, "%s", name.c_str());
    row("Nombre", 130); if (ImGui::InputText("##n", nb, sizeof nb)) name = nb;
    int mode = asTiles ? 1 : 0;
    row("Importar como", 130);
    if (ImGui::Combo("##m", &mode, "Sprite (imagen o hoja de animación)\0Conjunto de tiles\0")) { asTiles = mode == 1; if (asTiles) fw = fh = 16; else { fw = imgW; fh = imgH; } }
    if (asTiles) { row("Tamaño del tile", 130); ImGui::InputInt("##t", &fw); fh = fw; }
    else {
        row("Ancho del frame", 130); ImGui::InputInt("##fw", &fw);
        row("Alto del frame", 130); ImGui::InputInt("##fh", &fh);
        ImGui::TextDisabled("Con el tamaño de la imagen se importa un solo frame. Más pequeño, la imagen se trocea en frames (de izquierda a derecha y de arriba abajo).");
    }
    fw = std::clamp(fw, 1, std::max(1, imgW)); fh = std::clamp(fh, 1, std::max(1, imgH));
    int cols = std::max(1, imgW / fw), rows = std::max(1, imgH / (asTiles ? fw : fh));
    ImGui::Text("%d %s", cols * rows, asTiles ? "tile(s)" : "frame(s)");
    ImVec2 av = ImGui::GetContentRegionAvail();
    float sc = std::max(1.0f, std::floor(std::min((av.x - 20) / imgW, (av.y - 50) / imgH)));
    if (imgW * sc > av.x - 20 || imgH * sc > av.y - 50) sc = std::min((av.x - 20) / imgW, (av.y - 50) / imgH);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 e(p.x + imgW * sc, p.y + imgH * sc);
    ui::checkerboard(dl, p, e, 8);
    dl->AddImage(tex.id(), p, e);
    int gh = asTiles ? fw : fh;
    for (int c = 1; c < cols; c++) dl->AddLine(ImVec2(p.x + c * fw * sc, p.y), ImVec2(p.x + c * fw * sc, p.y + rows * gh * sc), IM_COL32(157, 125, 255, 200));
    for (int r = 1; r < rows; r++) dl->AddLine(ImVec2(p.x, p.y + r * gh * sc), ImVec2(p.x + cols * fw * sc, p.y + r * gh * sc), IM_COL32(157, 125, 255, 200));
    ImGui::Dummy(ImVec2(imgW * sc, imgH * sc));
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() - 8);
    if (ImGui::Button("Cancelar", ImVec2(120, 0))) { open = false; }
    ImGui::SameLine();
    if (ui::primaryButton("Importar", ImVec2(120, 0))) {
        bool t = asTiles; std::string id = commit(proj); done(id, t);
    }
    if (!open) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
