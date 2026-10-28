#include "school.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include "assets.hpp"
#include "ui.hpp"
#include "imgui_internal.h"

namespace school {
std::function<void(const std::string &name)> onNewProject;
std::function<void(const std::string &msg)> onNotify;

/* ---------------- lo que se mira en el proyecto ---------------- */
static const json &arrOf(const json &d, const char *k) { static const json empty = json::array(); return d.contains(k) && d[k].is_array() ? d[k] : empty; }
static bool isSprite(const json &o) { return jstr(o, "type", "sprite") == "sprite"; }
static bool painted(const json &doc, const std::string &spriteId, int minFrames = 1) {
    for (auto &s : arrOf(doc, "sprites")) {
        if (jstr(s, "id") != spriteId || !s.contains("frames") || !s["frames"].is_array()) continue;
        int ok = 0;
        for (auto &f : s["frames"]) {
            bool any = false;
            if (f.contains("pixels") && f["pixels"].is_array())
                for (auto &p : f["pixels"]) if (p.is_string() && p.get<std::string>().size() >= 7 && !(p.get<std::string>().size() == 9 && p.get<std::string>().substr(7) == "00")) { any = true; break; }
            ok += any;
        }
        return ok >= minFrames;
    }
    return false;
}
static int instancesOf(const json &doc, const std::string &objId) {
    int n = 0;
    for (auto &sc : arrOf(doc, "scenes")) for (auto &i : arrOf(sc, "instances")) n += jstr(i, "objectId") == objId;
    return n;
}
static bool platformer(const json &o) {
    return (o.contains("comportamientos") && o["comportamientos"].contains("plataformero")) || jstr(o.value("behaviors", json::object()), "movement") == "platform";
}
static bool solid(const json &o) { return jbool(o.value("behaviors", json::object()), "solid", false); }
/* cada evento (de todas las escenas y los globales): sus tipos de condiciones y acciones */
static void eachEvent(const json &doc, const std::function<void(const std::set<std::string> &, const std::set<std::string> &)> &f) {
    auto run = [&](const json &L) {
        for (auto &e : L) {
            if (!e.is_object() || e.contains("kind")) continue;
            std::set<std::string> c, a;
            for (auto &x : arrOf(e, "conditions")) c.insert(jstr(x, "type"));
            for (auto &x : arrOf(e, "actions")) a.insert(jstr(x, "type"));
            f(c, a);
        }
    };
    run(arrOf(doc, "events"));
    for (auto &sc : arrOf(doc, "scenes")) run(arrOf(sc, "events"));
}
static bool hasEvent(const json &doc, std::vector<std::string> conds, std::vector<std::string> acts, bool anyAct = false) {
    bool found = false;
    eachEvent(doc, [&](const std::set<std::string> &c, const std::set<std::string> &a) {
        bool ok = true;
        for (auto &x : conds) ok = ok && c.count(x);
        if (anyAct) { bool any = acts.empty(); for (auto &x : acts) any = any || a.count(x); ok = ok && any; }
        else for (auto &x : acts) ok = ok && a.count(x);
        found = found || ok;
    });
    return found;
}
static std::set<std::string> g_notes;

/* ---------------- lecciones ---------------- */
struct Step { const char *text, *hint; std::function<bool(const json &)> check; };
struct Lesson { const char *id, *title, *desc; std::vector<Step> steps; };
static bool noted(const char *k) { return g_notes.count(k) > 0; }
static const std::vector<Lesson> &lessons() {
    static std::vector<Lesson> L = {
        {"personaje", "1. Tu primer personaje", "Crea un objeto, dibújalo y ponlo en la escena.", {
            {"Crea un objeto: en el panel del proyecto (botón de menú arriba a la izquierda), Objetos > + y elige Sprite.",
             "Un objeto es cualquier cosa del juego: el personaje, una moneda, un enemigo.",
             [](const json &d) { for (auto &o : arrOf(d, "objects")) if (isSprite(o)) return true; return false; }},
            {"Dibuja tu personaje: abre el sprite del objeto (Editar sprite) y pinta con el lápiz (B).",
             "Clic izquierdo pinta con el color principal; clic derecho con el secundario.",
             [](const json &d) { for (auto &o : arrOf(d, "objects")) if (isSprite(o) && painted(d, jstr(o, "spriteId"))) return true; return false; }},
            {"Ponlo en la escena: en la pestaña de la escena, elige el objeto en la lista y haz clic donde quieras.",
             "Cada vez que lo pones es una instancia: puedes tener muchas del mismo objeto.",
             [](const json &d) { for (auto &o : arrOf(d, "objects")) if (isSprite(o) && instancesOf(d, jstr(o, "id"))) return true; return false; }},
            {"Pruébalo: pulsa Vista previa (o F5).", "En la vista previa, F3 muestra el depurador.", [](const json &) { return noted("preview"); }}}},
        {"mover", "2. Que se mueva y salte", "Comportamiento de plataformas y un suelo.", {
            {"Al objeto del personaje, en Comportamientos, añade Personaje plataformero.",
             "Los comportamientos dan al objeto una forma de moverse sin escribir eventos.",
             [](const json &d) { for (auto &o : arrOf(d, "objects")) if (platformer(o)) return true; return false; }},
            {"Crea otro objeto para el suelo, dibújalo y dale el comportamiento Plataforma.",
             "Plataforma lo hace sólido: los personajes chocan con él o se paran encima.",
             [](const json &d) { for (auto &o : arrOf(d, "objects")) if (solid(o) && !platformer(o)) return true; return false; }},
            {"Pon el suelo en la escena, debajo del personaje (estíralo con la escala si quieres).",
             "También puedes usar un mapa de tiles para el suelo.",
             [](const json &d) { for (auto &o : arrOf(d, "objects")) if (solid(o) && !platformer(o) && instancesOf(d, jstr(o, "id"))) return true; return false; }},
            {"Pruébalo: flechas para moverte y espacio para saltar (o un mando).", "", [](const json &) { return noted("preview"); }}}},
        {"evento", "3. Tu primer evento", "Condiciones y acciones: \"cuando pase esto, haz aquello\".", {
            {"Abre la hoja de eventos de la escena (pestaña Eventos) y pulsa + Añadir un evento.",
             "Un evento tiene condiciones (a la izquierda) y acciones (a la derecha).",
             [](const json &d) { bool any = false; eachEvent(d, [&](const std::set<std::string> &, const std::set<std::string> &) { any = true; }); return any; }},
            {"Ponle una condición: Otras condiciones > Entrada > Se acaba de pulsar la tecla.",
             "Elige la tecla que quieras, por ejemplo la Z.",
             [](const json &d) { return hasEvent(d, {"keyPressed"}, {}); }},
            {"Y una acción en ese mismo evento: Audio > Tocar un tono (o un efecto de sonido).",
             "Las acciones se hacen en orden, de arriba abajo, cuando se cumplen todas las condiciones.",
             [](const json &d) { return hasEvent(d, {"keyPressed"}, {"beep", "sfx", "playSound"}, true); }},
            {"Pruébalo y pulsa esa tecla.", "", [](const json &) { return noted("preview"); }}}},
        {"puntos", "4. Monedas y puntos", "Colisiones, variables y un marcador.", {
            {"Crea un objeto Moneda y pon al menos 3 en la escena.", "Puedes duplicar una instancia con Ctrl+D.",
             [](const json &d) { for (auto &o : arrOf(d, "objects")) if (isSprite(o) && !platformer(o) && !solid(o) && instancesOf(d, jstr(o, "id")) >= 3) return true; return false; }},
            {"En Ajustes del proyecto, crea una variable global llamada puntos.", "Las variables guardan números o textos mientras juegas.",
             [](const json &d) { for (auto &g : arrOf(d, "globals")) if (jstr(g, "name") == "puntos") return true; return false; }},
            {"Evento: condición Colisión (el personaje con la moneda); acciones Eliminar la moneda y Sumar a la variable global puntos.",
             "La condición elige qué moneda tocaste: solo esa se elimina.",
             [](const json &d) { return hasEvent(d, {"collision"}, {"removeObj", "addGlobal"}); }},
            {"Crea un objeto Texto con el contenido Puntos: {g:puntos} y ponlo en la escena.",
             "{g:puntos} se cambia por el valor de la variable al jugar.",
             [](const json &d) { for (auto &o : arrOf(d, "objects")) if (jstr(o, "type") == "text" && jstr(o.value("text", json::object()), "content").find("{g:") != std::string::npos && instancesOf(d, jstr(o, "id"))) return true; return false; }},
            {"Pruébalo y recoge las monedas.", "", [](const json &) { return noted("preview"); }}}},
        {"animar", "5. Animaciones", "Varios dibujos seguidos dan vida al personaje.", {
            {"En Source Sprite, en el sprite del personaje, añade frames con + y dibuja cada uno un poco distinto.",
             "Con la piel de cebolla (onion skin) ves el frame de antes mientras dibujas.",
             [](const json &d) { for (auto &s : arrOf(d, "sprites")) if (painted(d, jstr(s, "id"), 2)) return true; return false; }},
            {"En el objeto, pestaña Animaciones, añade una animación con ese sprite.",
             "Llámala Walk o asígnala a Caminar para que se use al moverse.",
             [](const json &d) { for (auto &o : arrOf(d, "objects")) if (o.contains("animations") && o["animations"].is_array() && !o["animations"].empty()) return true; return false; }},
            {"Pruébalo.", "", [](const json &) { return noted("preview"); }}}},
        {"escenas", "6. Escenas y menú", "Un menú que lleva al juego.", {
            {"Crea una segunda escena (panel del proyecto > Escenas > +).", "Cada nivel o pantalla (menú, juego, fin) es una escena.",
             [](const json &d) { return arrOf(d, "scenes").size() >= 2; }},
            {"En una escena, evento: Se acaba de pulsar la tecla Enter > acción Cambiar de escena.", "",
             [](const json &d) { return hasEvent(d, {"keyPressed"}, {"changeScene"}); }},
            {"Pruébalo y pulsa Enter.", "", [](const json &) { return noted("preview"); }}}},
        {"efectos", "7. Efectos que se sienten", "Partículas, temblor y pausa de impacto.", {
            {"En un evento (por ejemplo al recoger una moneda) añade la acción Emitir partículas.", "",
             [](const json &d) { return hasEvent(d, {}, {"particles"}); }},
            {"Añade Sacudir la cámara al chocar con algo.", "Poca fuerza y poco tiempo: 6 y 0.2 s ya se notan.",
             [](const json &d) { return hasEvent(d, {}, {"shakeCamera"}); }},
            {"Prueba también Pausa de impacto o Destello blanco.", "Un golpe se siente más si el juego se congela 0.05 s.",
             [](const json &d) { return hasEvent(d, {}, {"hitStop", "flashWhite"}, true); }},
            {"Pruébalo.", "", [](const json &) { return noted("preview"); }}}},
        {"publicar", "8. Publicar tu juego", "Un ejecutable para Windows y Linux, o la web.", {
            {"Pulsa Exportar, elige el tipo y para qué sistemas, y exporta.",
             "La exportación ligera ocupa muy poco; la web (HTML) sirve para itch.io y las game jams.",
             [](const json &) { return noted("export"); }}}},
    };
    return L;
}

/* ---------------- progreso ---------------- */
static std::map<std::string, int> g_done;      // pasos hechos de cada leccion
static bool g_loaded = false;
static std::string progressFile() { return ui::configDir() + "/escuela.json"; }
static void load() {
    if (g_loaded) return;
    g_loaded = true;
    if (!saveProgress) return;
    std::ifstream in(progressFile());
    json j = json::parse(in, nullptr, false);
    if (j.is_object()) for (auto it = j.begin(); it != j.end(); ++it) if (it.value().is_number()) g_done[it.key()] = it.value().get<int>();
}
bool saveProgress = true;                         // las pruebas no tocan el progreso de verdad
static void save() {
    if (!saveProgress) return; json j = json::object(); for (auto &kv : g_done) j[kv.first] = kv.second; std::ofstream(progressFile()) << j.dump(1); }

static int g_lesson = -1, g_step = 0;
static double g_doneAt = -1;                    // cuando se cumplio el paso actual (para la animacion)
static bool g_hint = false, g_min = false;
bool active() { return g_lesson >= 0; }
int stepIndex() { return g_step; }
void note(const std::string &what) { g_notes.insert(what); }
void start(const std::string &id) {
    load();
    const auto &L = lessons();
    for (int i = 0; i < (int)L.size(); i++) if (L[i].id == id) {
        g_lesson = i; g_step = std::min(g_done[id], (int)L[i].steps.size() - 1); g_doneAt = -1; g_hint = false; g_min = false;
        if (g_done[id] >= (int)L[i].steps.size()) g_step = 0;
        g_notes.clear();
    }
}

void drawSection() {
    load();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "Aprende haciendo: cada lección te dice qué hacer y se da cuenta sola cuando lo haces.");
    ImGui::Dummy(ImVec2(0, 8));
    const auto &L = lessons();
    float w = std::min(560.0f, ImGui::GetContentRegionAvail().x);
    for (int i = 0; i < (int)L.size(); i++) {
        const Lesson &l = L[i];
        int done = std::min(g_done[l.id], (int)l.steps.size());
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        float h = 74;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ui::kPanel, 10);
        bool complete = done >= (int)l.steps.size();
        dl->AddText(ui::boldFont(), ImGui::GetFontSize() + 3, ImVec2(p.x + 16, p.y + 12), complete ? IM_COL32(120, 220, 150, 255) : ui::kText, l.title);
        dl->AddText(ImVec2(p.x + 16, p.y + 38), ui::kText2, l.desc);
        float bw = 140, frac = (float)done / (float)l.steps.size();
        dl->AddRectFilled(ImVec2(p.x + 16, p.y + h - 10), ImVec2(p.x + w - bw - 30, p.y + h - 6), ui::kLine, 2);
        dl->AddRectFilled(ImVec2(p.x + 16, p.y + h - 10), ImVec2(p.x + 16 + (w - bw - 46) * frac, p.y + h - 6), complete ? IM_COL32(90, 200, 130, 255) : ui::kAccent, 2);
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - bw - 14, p.y + 20));
        const char *lbl = complete ? "Repetir" : done ? "Seguir" : "Empezar";
        if (ui::primaryButton(lbl, ImVec2(bw, 34))) {
            if (onNewProject) onNewProject(std::string("Escuela - ") + l.title);
            start(l.id);
            if (complete) { g_step = 0; }
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 10));
        ImGui::PopID();
    }
}

void drawPanel(Project &proj) {
    if (g_lesson < 0) return;
    const Lesson &l = lessons()[g_lesson];
    int n = (int)l.steps.size();
    if (g_step >= n) g_step = n - 1;
    const Step &s = l.steps[g_step];
    double now = ImGui::GetTime();
    if (g_doneAt < 0 && s.check(proj.doc)) {                      // paso cumplido
        g_doneAt = now;
        g_done[l.id] = std::max(g_done[l.id], g_step + 1);
        save();
    }
    if (g_doneAt >= 0 && now - g_doneAt > 1.2) {                    // tras un momento, el siguiente
        g_doneAt = -1; g_hint = false;
        if (g_step + 1 < n) g_step++;
        else { g_lesson = -1; if (onNotify) onNotify("Lección terminada: " + std::string(l.title)); return; }
    }
    ImGuiViewport *vp = ImGui::GetMainViewport();
    float W = 400;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - W - 18, vp->WorkPos.y + vp->WorkSize.y - 18), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(W, 0));
    ImGui::SetNextWindowBgAlpha(0.97f);
    if (ImGui::Begin("##escuela", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kAccent), "Escuela");
        ImGui::SameLine(); ImGui::TextDisabled("%s", l.title);
        ImGui::SameLine(W - 70);
        if (ImGui::SmallButton(g_min ? "Abrir" : "Ocultar")) g_min = !g_min;
        if (!g_min) {
            ImVec2 bp = ImGui::GetCursorScreenPos();
            float frac = (float)(g_step + (g_doneAt >= 0 ? 1 : 0)) / (float)n;
            dl->AddRectFilled(bp, ImVec2(bp.x + W - 16, bp.y + 4), ui::kLine, 2);
            dl->AddRectFilled(bp, ImVec2(bp.x + (W - 16) * ui::animTo(ImGui::GetID("##esc_bar"), frac, 8.0f), bp.y + 4), ui::kAccent, 2);
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextDisabled("Paso %d de %d", g_step + 1, n);
            ImGui::PushTextWrapPos(W - 16);
            if (g_doneAt >= 0) ImGui::TextColored(ImVec4(0.45f, 0.86f, 0.55f, 1), "¡Bien hecho!");
            ImGui::TextUnformatted(s.text);
            if (g_hint && s.hint && *s.hint) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "%s", s.hint);
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 4));
            if (s.hint && *s.hint && ImGui::SmallButton(g_hint ? "Ocultar pista" : "Pista")) g_hint = !g_hint;
            ImGui::SameLine();
            if (g_step > 0 && ImGui::SmallButton("Anterior")) { g_step--; g_doneAt = -1; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Saltar")) { g_doneAt = -1; if (g_step + 1 < n) g_step++; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Salir")) g_lesson = -1;
        }
    }
    ImGui::End();
}
}
