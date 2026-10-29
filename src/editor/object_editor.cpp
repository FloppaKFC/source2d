#include "object_editor.hpp"
#include "code_editor.hpp"
#include "school.hpp"
#include "collision_editor.hpp"
#include "tileset_editor.hpp"
#include "tiles_obj.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

/* ---------------- creacion ---------------- */
std::string createObject(Project &proj, const std::string &kind) {
    json &objs = proj.doc["objects"];
    int n = (int)objs.size() + 1;
    std::string id = makeUid("o");
    json o = {{"id", id}, {"name", "Objeto " + std::to_string(n)}, {"type", kind}, {"spriteId", nullptr}, {"tilesetId", nullptr},
              {"hud", kind == "text" || kind == "button"}, {"shader", ""}, {"hooks", {{"onCreate", ""}, {"onUpdate", ""}}},
              {"behaviors", {{"gravity", false}, {"solid", false}, {"oneway", false}, {"collide", false}, {"movement", "none"}, {"speed", 150},
                             {"jump", 620}, {"friction", 0.85}, {"airFriction", 0.99}, {"bounce", 0}, {"mass", 1}, {"spin", 0}}},
              {"vars", json::array()}, {"animations", json::array()}, {"flipOnMove", true}, {"comportamientos", json::object()}};
    if (kind == "sprite" && !proj.sprites().empty()) o["spriteId"] = jstr(proj.sprites()[0], "id");
    if (kind == "sprite" && proj.sprites().empty()) {   // el primero: con su sprite en blanco listo para dibujar
        json px = json::array();
        for (int i = 0; i < 32 * 32; i++) px.push_back("");
        std::string sid = makeUid("s");
        proj.sprites().push_back({{"id", sid}, {"name", "Objeto " + std::to_string(n)}, {"width", 32}, {"height", 32}, {"frames", json::array({{{"pixels", px}}})}});
        o["spriteId"] = sid;
    }
    if (kind == "tile") {   // cada objeto Tile tiene su mapa de tiles (el primero del proyecto o uno nuevo)
        if (proj.doc.contains("tilesets") && proj.doc["tilesets"].is_array() && !proj.doc["tilesets"].empty()) o["tilesetId"] = jstr(proj.doc["tilesets"][0], "id");
        else {
            o["tilesetId"] = createTileset(proj, "Tiles 1", 16, 4, 2);
            if (json *ts = findById(proj.doc["tilesets"], jstr(o, "tilesetId"))) starterTiles(*ts);
        }
    }
    if (kind == "light") { o["name"] = "Luz " + std::to_string(n); o["light"] = {{"radius", 160}, {"intensity", 1}, {"color", "#ffd9a0"}, {"flicker", 0}}; }
    if (kind == "text") { o["name"] = "Texto " + std::to_string(n); o["text"] = {{"content", "Texto"}, {"size", 24}, {"color", "#ffffff"}, {"align", "left"}}; }
    if (kind == "button") { o["name"] = "Boton " + std::to_string(n); o["button"] = {{"text", "Boton"}, {"fontSize", 15}, {"bg", "#3a4150"}, {"fg", "#ffffff"}, {"width", 150}, {"height", 36}, {"radius", 5}}; }
    if (kind == "tile") o["name"] = "Tile " + std::to_string(n);
    if (kind == "group") { o["name"] = "Grupo " + std::to_string(n); o["members"] = json::array(); }
    objs.push_back(o);
    proj.dirty = true;
    return id;
}

/* ---------------- comportamientos (mismo modelo que el runtime) ---------------- */
struct BhProp { const char *k, *label, *type; double d; const char *sdef; std::vector<std::pair<std::string, std::string>> opts; };
struct BhInfo { const char *type, *name, *desc; bool move; std::vector<BhProp> props; };
static const std::vector<BhInfo> &catalog() {
    static const std::vector<BhInfo> c = {
        {"platformer", "Personaje plataformero", "Camina, salta y cae sobre plataformas con las flechas y la barra espaciadora.", true, {}},
        {"topdown", "Movimiento cenital", "Se mueve en 8 direcciones con las flechas o WASD, visto desde arriba.", true, {}},
        {"rts", "Unidad de estrategia", "Se selecciona con el ratón y va al punto donde hagas clic.", true, {}},
        {"platform", "Plataforma", "Los personajes chocan con ella o se paran encima: sólida, traspasable o escalera.", false, {}},
        {"movingPlatform", "Plataforma móvil", "Va y viene entre dos puntos y lleva encima a lo que esté sobre ella.", false, {}},
        {"draggable", "Arrastrable", "Se puede arrastrar con el ratón.", false, {}},
        {"destroyOutside", "Destruir fuera de la pantalla", "Se elimina solo al salir de la vista de la cámara. Ideal para balas.", false, {{"margin", "Margen extra (px)", "number", 32, "", {}}}},
        {"sine", "Movimiento sinusoidal", "Oscila suavemente: flota, va y viene o se balancea.", false,
         {{"axis", "Eje", "select", 0, "y", {{"x", "Horizontal"}, {"y", "Vertical"}, {"angle", "Ángulo"}}}, {"amplitude", "Amplitud", "number", 12, "", {}},
          {"period", "Periodo (s)", "number", 2, "", {}}, {"phase", "Desfase (grados)", "number", 0, "", {}}}},
        {"flash", "Parpadeo", "Permite hacerlo parpadear con una acción, por ejemplo al recibir daño.", false, {}},
        {"health", "Salud", "Puntos de vida con daño y curación. En expresiones: Objeto.Salud.", false,
         {{"max", "Salud máxima", "number", 100, "", {}}, {"start", "Salud inicial", "text", 0, "", {}}}},
        {"fireBullet", "Disparador", "Dispara otros objetos como proyectiles, con tiempo de enfriamiento.", false, {{"cooldown", "Enfriamiento (s)", "number", 0.25, "", {}}}},
        {"patrol", "Patrullar", "Camina de un lado a otro y se da la vuelta en las paredes (y en los bordes, si quieres).", false,
         {{"speed", "Velocidad", "number", 60, "", {}}, {"edges", "En los bordes", "select", 0, "turn", {{"turn", "Se da la vuelta"}, {"fall", "Se cae"}}}}},
        {"follow", "Seguir", "Va hacia otro objeto (por su nombre) y se para a cierta distancia.", false,
         {{"target", "Objeto a seguir", "text", 0, "Jugador", {}}, {"speed", "Velocidad", "number", 90, "", {}}, {"stop", "Se para a (px)", "number", 24, "", {}},
          {"range", "Lo ve desde (px, 0 = siempre)", "number", 0, "", {}}}},
        {"light", "Luz", "Ilumina la oscuridad de la escena (Escena > Luz) a su alrededor.", false,
         {{"radius", "Radio (px)", "number", 160, "", {}}, {"intensity", "Intensidad (0-1)", "number", 1, "", {}},
          {"color", "Color del brillo", "text", 0, "#ffd9a0", {}}, {"flicker", "Parpadeo (0-1)", "number", 0, "", {}}}},
        {"touchKey", "Botón táctil", "Al tocarlo (o hacer clic) cuenta como pulsar una tecla: controles en pantalla para móviles.", false,
         {{"key", "Tecla", "text", 0, "arrowleft", {}}}},
        {"keepOnScreen", "Mantener en pantalla", "No puede salir de la vista de la cámara (naves, jugadores de arcade).", false, {}},
    };
    return c;
}
static const BhInfo *bhInfo(const std::string &t) {
    for (auto &b : catalog()) if (t == b.type) return &b;
    return nullptr;
}
static json &sub(json &o, const char *key) {
    if (!o.contains(key) || !o[key].is_object()) o[key] = json::object();
    return o[key];
}
static std::string platType(const json &b) {
    if (jbool(b, "ladder", false)) return "ladder";
    if (jbool(b, "solid", false) && jbool(b, "oneway", false)) return "oneway";
    if (jbool(b, "solid", false)) return "solid";
    return "none";
}
static void applyPlatType(json &b, const std::string &t) {
    b["solid"] = t == "solid" || t == "oneway"; b["oneway"] = t == "oneway"; b["ladder"] = t == "ladder";
}

std::vector<std::string> objBehaviorTypes(const json &o) {
    std::vector<std::string> out;
    json b = o.contains("behaviors") ? o["behaviors"] : json::object(), cp = o.contains("comportamientos") ? o["comportamientos"] : json::object();
    std::string mv = jstr(b, "movement", "none");
    if (cp.contains("plataformero") || mv == "platform") out.push_back("platformer");
    if (mv == "topdown") out.push_back("topdown");
    if (mv == "rts") out.push_back("rts");
    if (platType(b) != "none") out.push_back("platform");
    if (cp.contains("plataforma")) out.push_back("movingPlatform");
    if (o.contains("behaviorList") && o["behaviorList"].is_array())
        for (auto &x : o["behaviorList"]) { std::string t = jstr(x, "type"); if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t); }
    return out;
}

static void bhRemove(json &o, const std::string &type) {
    json &b = sub(o, "behaviors"), &cp = sub(o, "comportamientos");
    if (type == "platformer") { cp.erase("plataformero"); if (jstr(b, "movement") == "platform") b["movement"] = "none"; }
    else if (type == "topdown" || type == "rts") { if (jstr(b, "movement") == type) b["movement"] = "none"; }
    else if (type == "platform") applyPlatType(b, "none");
    else if (type == "movingPlatform") cp.erase("plataforma");
    else if (o.contains("behaviorList")) {
        json &l = o["behaviorList"];
        for (size_t i = 0; i < l.size(); i++) if (jstr(l[i], "type") == type) { l.erase(l.begin() + i); break; }
    }
}

static void bhAdd(json &o, const std::string &type) {
    const BhInfo *info = bhInfo(type);
    json &b = sub(o, "behaviors"), &cp = sub(o, "comportamientos");
    if (info && info->move) for (const char *t : {"platformer", "topdown", "rts"}) if (type != t) bhRemove(o, t);
    if (type == "platformer") { if (!cp.contains("plataformero")) cp["plataformero"] = {{"keyLeft", ""}, {"keyRight", ""}, {"keyJump", ""}}; b["movement"] = "platform"; b["gravity"] = true; b["collide"] = true; }
    else if (type == "topdown" || type == "rts") { b["movement"] = type; b["collide"] = true; b["gravity"] = false; }
    else if (type == "platform") applyPlatType(b, "solid");
    else if (type == "movingPlatform") cp["plataforma"] = {{"dx", 120}, {"dy", 0}, {"speed", 60}};
    else if (info) {
        if (!o.contains("behaviorList") || !o["behaviorList"].is_array()) o["behaviorList"] = json::array();
        json cfg = {{"type", type}};
        for (auto &p : info->props) cfg[p.k] = std::string(p.type) != "number" ? json(p.sdef) : (p.d == (long long)p.d ? json((long long)p.d) : json(p.d));
        o["behaviorList"].push_back(cfg);
    }
}

void objectAddBehavior(json &obj, const std::string &type) { bhAdd(obj, type); }
void objectRemoveBehavior(json &obj, const std::string &type) { bhRemove(obj, type); }

static void animPreview(Project &proj, ImDrawList *dl, const json &anim, ImVec2 a, ImVec2 b);

/* ---------------- interfaz ---------------- */
std::string ObjectEditor::title(Project &proj) {
    json *o = findById(proj.doc["objects"], objectId);
    return "Objeto: " + (o ? jstr(*o, "name") : std::string("?")) + "###obj_" + objectId;
}

static void labelRow(const char *l) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::SameLine(190); ImGui::SetNextItemWidth(-1); }

// fuente del texto: la del juego o una del proyecto (TTF importada o de pixeles)
static void fontRows(Project &proj, json &t) {
    if (!proj.doc.contains("fonts") || !proj.doc["fonts"].is_array()) proj.doc["fonts"] = json::array();
    labelRow("Fuente");
    if (comboById("##font", t, "font", proj.doc["fonts"], "La del juego")) proj.rev++;
    json *ff = findById(proj.doc["fonts"], jstr(t, "font"));
    if (!ff || jstr(*ff, "kind") != "pixel") { labelRow("Negrita"); editBool("##bold", t, "bold", true); }
    ImGui::TextDisabled("Crea o importa fuentes en el panel del proyecto, apartado Fuentes.");
}

void ObjectEditor::tabProps(Project &proj, json &o) {
    std::string type = jstr(o, "type", "sprite");
    ImGui::SeparatorText("General");
    labelRow("Nombre"); editText("##name", o, "name");
    school::anchor("obj.name");
    {   // objeto de una escena o global (en todas las escenas)
        bool global = jstr(o, "scene").empty();
        std::string owner;
        if (!global) for (auto &sc : proj.doc["scenes"]) if (jstr(sc, "id") == jstr(o, "scene")) owner = jstr(sc, "name");
        labelRow("Escena");
        ImGui::TextUnformatted(global ? "Todas (objeto global)" : owner.c_str());
        ImGui::Dummy(ImVec2(182, 0)); ImGui::SameLine();
        if (ImGui::Checkbox("Objeto global (lo pueden usar todas las escenas)", &global)) {
            if (global) o.erase("scene");
            else {   // vuelve a la primera escena donde se usa (o a la primera escena)
                std::string sid;
                for (auto &sc : proj.doc["scenes"]) if (sid.empty() && sc.contains("instances")) for (auto &it : sc["instances"]) if (jstr(it, "objectId") == objectId) sid = jstr(sc, "id");
                if (sid.empty() && !proj.doc["scenes"].empty()) sid = jstr(proj.doc["scenes"][0], "id");
                o["scene"] = sid;
            }
            proj.dirty = true; proj.rev++;
        }
    }
    ImGui::TextDisabled("Si lo renombras, actualiza las expresiones de los eventos que usen el nombre (Jugador.X).");
    if (type == "sprite") {
        labelRow("Dibujo");   // las animaciones y su colision, en la pestana Animaciones
        ImGui::TextDisabled("Las animaciones y sus colisiones están en la pestaña Animaciones.");
        ImGui::Dummy(ImVec2(182, 0)); ImGui::SameLine();
        bool esHit = ImGui::Button("Editar sprite");
        school::anchor("obj.editsprite");
        if (esHit && !jstr(o, "spriteId").empty() && onOpenSprite) onOpenSprite(jstr(o, "spriteId"));
        ImGui::SameLine();
        if (ImGui::Button("Nuevo sprite") && onNewSprite) { std::string id = onNewSprite(jstr(o, "name"), 32, 32); o["spriteId"] = id; if (onOpenSprite) onOpenSprite(id); }
    } else if (type == "text") {
        json &t = sub(o, "text");
        labelRow("Texto"); editText("##content", t, "content");
        ImGui::TextDisabled("Usa {g:variable} para mostrar una variable global.");
        labelRow("Tamaño"); editNum("##size", t, "size", 24, 0.5f, "%.0f px", 4, 400);
        labelRow("Color"); editColor("##color", t, "color", "#ffffff");
        labelRow("Alineación"); comboOptions("##align", t, "align", {{"left", "Izquierda"}, {"center", "Centro"}, {"right", "Derecha"}}, "left");
        fontRows(proj, t);
    } else if (type == "button") {
        json &b = sub(o, "button");
        labelRow("Texto"); editText("##btext", b, "text");
        labelRow("Ancho"); editNum("##bw", b, "width", 160, 1, "%.0f px", 8, 2000);
        labelRow("Alto"); editNum("##bh", b, "height", 42, 1, "%.0f px", 8, 2000);
        labelRow("Letra"); editNum("##bfs", b, "fontSize", 16, 0.5f, "%.0f px", 6, 200);
        labelRow("Esquinas"); editNum("##brad", b, "radius", 6, 0.5f, "%.0f px", 0, 100);
        labelRow("Fondo"); editColor("##bbg", b, "bg", "#3a4150");
        labelRow("Color del texto"); editColor("##bfg", b, "fg", "#ffffff");
        fontRows(proj, b);
    } else if (type == "group") {
        // grupo: lista de objetos; en los eventos, el grupo vale por todos sus objetos
        if (!o.contains("members") || !o["members"].is_array()) o["members"] = json::array();
        ImGui::SeparatorText("Objetos del grupo");
        for (auto &m : proj.doc["objects"]) {
            std::string mid = jstr(m, "id"), mt = jstr(m, "type", "sprite");
            if (mid == jstr(o, "id") || mt == "group" || mt == "tile") continue;
            json &mem = o["members"];
            auto it = std::find(mem.begin(), mem.end(), json(mid));
            bool in = it != mem.end();
            if (ImGui::Checkbox((jstr(m, "name") + "##" + mid).c_str(), &in)) {
                if (in) mem.push_back(mid); else mem.erase(it);
                proj.dirty = true;
            }
        }
    } else if (type == "tile") {
        if (!proj.doc.contains("tilesets")) proj.doc["tilesets"] = json::array();
        labelRow("Mapa de tiles"); if (comboById("##ts", o, "tilesetId", proj.doc["tilesets"], "(ninguno)")) proj.rev++;
        ImGui::Dummy(ImVec2(182, 0)); ImGui::SameLine();
        if (ImGui::Button("Editar tiles y colisiones") && !jstr(o, "tilesetId").empty() && onOpenTileset) onOpenTileset(jstr(o, "tilesetId"));
        ImGui::SameLine();
        if (ImGui::Button("Nuevo mapa de tiles")) {
            o["tilesetId"] = createTileset(proj, jstr(o, "name") + " tiles", 16, 4, 2);
            proj.rev++;
            if (onOpenTileset) onOpenTileset(jstr(o, "tilesetId"));
        }
        ImGui::SameLine();
        if (ImGui::Button("Importar imagen...") && onImportTiles) onImportTiles(jstr(o, "id"));
        ImGui::Dummy(ImVec2(182, 0)); ImGui::SameLine();
        editBool("Activar autotile", o, "autotile", false);
        if (jbool(o, "autotile", false)) {
            labelRow("Autotile"); comboById("##at", o, "autotileId", projectAutotiles(proj), "(elige uno)");
            if (projectAutotiles(proj).empty()) { ImGui::Dummy(ImVec2(182, 0)); ImGui::SameLine(); ImGui::TextDisabled("Crea uno en el panel del proyecto > Autotiles."); }
        }
        ImGui::TextDisabled("Píntalo en la escena: elige el objeto en la lista de objetos de la escena. Cada instancia\n"
                            "es un mapa que crece y se achica solo, y se puede mover entero. Las colisiones son las de sus tiles.");
    } else if (type == "light") {
        json &L = sub(o, "light");
        labelRow("Radio"); editNum("##lr", L, "radius", 160, 1, "%.0f px", 4, 4000);
        labelRow("Intensidad"); editNum("##li", L, "intensity", 1, 0.01f, "%.2f", 0, 1);
        labelRow("Color del brillo"); editColor("##lc", L, "color", "#ffd9a0");
        labelRow("Parpadeo"); editNum("##lf", L, "flicker", 0, 0.01f, "%.2f", 0, 1);
        ImGui::TextDisabled("Ilumina la oscuridad de las capas (Capas > Oscuridad). Ponla en la escena como cualquier objeto.");
    }
    if (type == "sprite") {   // nine-slice: al escalarlo, las esquinas y los bordes no se estiran (paneles, botones, barras)
        json &nn = sub(o, "nine");
        labelRow("Nine-slice (bordes)");
        float w4 = ImGui::GetContentRegionAvail().x / 4 - 6;
        ImGui::SetNextItemWidth(w4); editNum("##nl", nn, "l", 0, 0.5f, "izq %.0f", 0, 512); ImGui::SameLine();
        ImGui::SetNextItemWidth(w4); editNum("##nr", nn, "r", 0, 0.5f, "der %.0f", 0, 512); ImGui::SameLine();
        ImGui::SetNextItemWidth(w4); editNum("##nt", nn, "t", 0, 0.5f, "arr %.0f", 0, 512); ImGui::SameLine();
        ImGui::SetNextItemWidth(w4); editNum("##nb", nn, "b", 0, 0.5f, "abj %.0f", 0, 512);
    }
    if (type == "sprite") {   // caja de colision propia: mas pequena que el dibujo hace los saltos y los peligros mas justos
        json &hb = sub(o, "hitbox");
        labelRow("Caja de colisión");
        float w = ImGui::GetContentRegionAvail().x / 2 - 4;
        ImGui::SetNextItemWidth(w); editNum("##hbw", hb, "w", 0, 0.5f, "ancho %.0f px", 0, 4096);
        ImGui::SameLine(); ImGui::SetNextItemWidth(w); editNum("##hbh", hb, "h", 0, 0.5f, "alto %.0f px", 0, 4096);
        ImGui::TextDisabled("0 = el tamaño del sprite. Va centrada y no cambia con la escala (estirar/aplastar es solo visual).");
    }
    json layers = json::array({{{"id", "base"}, {"name", "Principal"}}});
    if (proj.doc.contains("layers") && proj.doc["layers"].is_array()) for (auto &L : proj.doc["layers"]) if (jstr(L, "id") != "base") layers.push_back(L);
    if (jstr(o, "capa").empty()) o["capa"] = "base";
    labelRow("Capa"); comboById("##capa", o, "capa", layers);
    ImGui::Dummy(ImVec2(182, 0)); ImGui::SameLine(); editBool("Elemento de interfaz (fijo en la pantalla, no sigue a la cámara)", o, "hud", false);

    if (type == "sprite") {
        json &b = sub(o, "behaviors");
        ImGui::SeparatorText("Física");
        editBool("Gravedad (cae)", b, "gravity", false); ImGui::SameLine(260);
        editBool("Choca con el escenario y los sólidos", b, "collide", false);
        // material: rellena rebote, rozamientos, masa, peso y resistencia del aire con valores de ese material (luego se
        // pueden retocar a mano). Entre dos cuerpos libres, el choque reparte la velocidad segun la masa.
        struct Mat { const char *id, *name; double bounce, fr, afr, mass, weight, drag, maxFall; };
        static const Mat mats[] = {
            {"", "Normal (a mano)", 0, 0.85, 0.99, 1, 1, 0, 0},
            {"piedra", "Piedra (pesada, casi no rebota)", 0.05, 0.6, 0.995, 3, 1, 0, 0},
            {"metal", "Metal (muy pesado)", 0.12, 0.7, 0.998, 8, 1, 0, 0},
            {"madera", "Madera", 0.25, 0.75, 0.99, 0.7, 1, 0, 0},
            {"goma", "Goma / hule (rebota mucho)", 0.8, 0.9, 0.99, 1, 1, 0, 0},
            {"pelota", "Pelota (rebota casi igual)", 0.92, 0.95, 0.995, 0.5, 1, 0.002, 0},
            {"hielo", "Hielo (resbala)", 0.05, 0.995, 0.999, 0.9, 1, 0, 0},
            {"papel", "Papel (cae despacio)", 0.02, 0.7, 0.9, 0.05, 0.35, 0.06, 140},
            {"pluma", "Pluma (flota al caer)", 0, 0.6, 0.85, 0.01, 0.15, 0.1, 60},
            {"globo", "Globo (sube)", 0.6, 0.9, 0.97, 0.02, -0.25, 0.04, 0},
        };
        std::string cur = jstr(b, "material");
        const Mat *sel = &mats[0];
        for (auto &m : mats) if (cur == m.id) sel = &m;
        labelRow("Material");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##mat", sel->name)) {
            for (auto &m : mats) if (ImGui::Selectable(m.name, &m == sel)) {
                b["material"] = m.id;
                if (*m.id) {
                    b["bounce"] = m.bounce; b["friction"] = m.fr; b["airFriction"] = m.afr; b["mass"] = m.mass;
                    b["weight"] = m.weight; b["drag"] = m.drag; b["maxFall"] = m.maxFall;
                }
            }
            ImGui::EndCombo();
        }
        labelRow("Rozamiento"); editNum("##fr", b, "friction", 0.85, 0.005f, "%.2f", 0, 1);
        labelRow("Rozamiento en aire"); editNum("##afr", b, "airFriction", 0.99, 0.005f, "%.2f", 0, 1);
        labelRow("Rebote"); editNum("##bo", b, "bounce", 0, 0.01f, "%.2f", 0, 1);
        labelRow("Masa"); editNum("##ma", b, "mass", 1, 0.05f, "%.2f", 0.01, 1000);
        labelRow("Peso (gravedad)"); editNum("##we", b, "weight", 1, 0.01f, "x%.2f", -5, 5);
        labelRow("Resistencia del aire"); editNum("##dr", b, "drag", 0, 0.002f, "%.3f", 0, 0.5);
        labelRow("Caída máxima"); editNum("##mf", b, "maxFall", 0, 5, "%.0f px/s (0 = sin límite)", 0, 5000);
        ImGui::TextDisabled("Masa: quién empuja a quién al chocar dos objetos libres. Peso: 1 = normal, 0.3 = cae como una hoja,\n"
                            "negativo = sube. Para que choquen entre sí, márcalos también como sólidos.");
        labelRow("Rotación continua"); editNum("##sp", b, "spin", 0, 1, "%.0f grados/s", -3600, 3600);
        ImGui::Dummy(ImVec2(182, 0)); ImGui::SameLine(); editBool("Estirar al saltar y aplastar al caer", o, "squash", false);
        ImGui::Dummy(ImVec2(182, 0)); ImGui::SameLine(); editBool("Sigue animándose con el juego en pausa (menús)", o, "ignorePause", false);
    }
    ImGui::SeparatorText("Código del objeto (JavaScript)");
    json &h = sub(o, "hooks");
    ImGui::TextDisabled("Al crearse  ($.inst es la instancia, $.vars las variables globales)");
    codeField("##oncreate", h, "onCreate", ImVec2(-1, 110), CodeLang::JS, true);
    ImGui::TextDisabled("En cada fotograma");
    codeField("##onupdate", h, "onUpdate", ImVec2(-1, 110), CodeLang::JS, true);
}

// operaciones con los frames de un sprite: la imagen final (frames), la de cada capa (layers[].frames) y la
// duracion de cada frame (durations) se mueven juntas. op: 'm' mover i a j, 'd' borrar i, 'c' duplicar i, 'n' frame vacio al final
void objectFrameOp(json &sp, char op, int i, int j) {
    auto apply = [&](json &arr, const json &blank) {
        if (!arr.is_array()) return;
        int n = (int)arr.size();
        if (op == 'n') { arr.push_back(blank); return; }
        if (i < 0 || i >= n) return;
        if (op == 'd') arr.erase(arr.begin() + i);
        else if (op == 'c') arr.insert(arr.begin() + i + 1, arr[i]);
        else if (op == 'm' && j >= 0 && j < n && j != i) { json v = arr[i]; arr.erase(arr.begin() + i); arr.insert(arr.begin() + j, v); }
    };
    int w = (int)jnum(sp, "width", 32), h = (int)jnum(sp, "height", 32);
    json empty = json::array();
    for (int k = 0; k < w * h; k++) empty.push_back("");
    if (!sp.contains("frames") || !sp["frames"].is_array()) sp["frames"] = json::array();
    apply(sp["frames"], json{{"pixels", empty}});
    if (sp.contains("layers") && sp["layers"].is_array()) for (auto &L : sp["layers"]) if (L.contains("frames")) apply(L["frames"], empty);
    if (sp.contains("durations") && sp["durations"].is_array() && !sp["durations"].empty()) apply(sp["durations"], sp["durations"].back());
}

// vista previa animada de una animacion (su sprite a sus fps), centrada en el cuadro a-b
static void animPreview(Project &proj, ImDrawList *dl, const json &anim, ImVec2 a, ImVec2 b) {
    ui::checkerboard(dl, a, b, 6);
    json *sp = proj.spriteById(jstr(anim, "spriteId"));
    int n = sp && (*sp)["frames"].is_array() ? (int)(*sp)["frames"].size() : 0;
    if (!n) return;
    int fps = std::max(1, (int)jnum(anim, "fps", 8));
    int f = (int)(ImGui::GetTime() * fps) % n;
    if (!jbool(anim, "loop", true)) f = std::min(n - 1, (int)(std::fmod(ImGui::GetTime(), (n + fps) / (double)fps) * fps));
    if (ui::PixTexture *tx = spriteFrameTex(proj, jstr(*sp, "id"), f)) {
        float bw = b.x - a.x - 8, bh = b.y - a.y - 8, sc = std::min(bw / tx->w, bh / tx->h), w = tx->w * sc, h = tx->h * sc;
        ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
        dl->AddImage(tx->id(), ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2));
    }
}

// tira de frames de una animacion: arrastrar para cambiar el orden, x para borrar, clic derecho para mas
void ObjectEditor::framesStrip(Project &proj, json &anim) {
    json *sp = proj.spriteById(jstr(anim, "spriteId"));
    if (!sp) { ImGui::TextDisabled("Esta animación no tiene sprite: elige uno arriba."); return; }
    json &frames = (*sp)["frames"];
    int n = frames.is_array() ? (int)frames.size() : 0;
    float cell = 72, gap = 8;
    // vista previa animada a la izquierda (fuera de la tira, que se desliza sin taparla)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(cell + 24, cell + 20));
        animPreview(proj, dl, anim, p, ImVec2(p.x + cell + 24, p.y + cell + 24));
        dl->AddText(ImVec2(p.x + 4, p.y + cell + 26), ui::kDim, "Vista previa");
        ImGui::SameLine(0, 16);
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));   // sin el fondo de la tarjeta encima otra vez
    ImGui::BeginChild("##strip", ImVec2(0, cell + 52), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleColor();
    ImDrawList *dl = ImGui::GetWindowDrawList();   // la de la tira: recorta lo que se sale por los lados
    int moveFrom = -1, moveTo = -1, del = -1, dup = -1;
    for (int i = 0; i < n; i++) {
        ImGui::PushID(i);
        if (i) ImGui::SameLine(0, gap);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##fr", ImVec2(cell, cell + 18));
        bool hov = ImGui::IsItemHovered();
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("S2D_FRAME", &i, sizeof i);
            ImGui::Text("Frame %d", i + 1);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("S2D_FRAME")) { moveFrom = *(const int *)pl->Data; moveTo = i; }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("##fctx")) {
            if (ImGui::MenuItem("Duplicar")) dup = i;
            if (ImGui::MenuItem("Mover a la izquierda", nullptr, false, i > 0)) { moveFrom = i; moveTo = i - 1; }
            if (ImGui::MenuItem("Mover a la derecha", nullptr, false, i + 1 < n)) { moveFrom = i; moveTo = i + 1; }
            ImGui::Separator();
            if (ImGui::MenuItem("Eliminar", nullptr, false, n > 1)) del = i;
            ImGui::EndPopup();
        }
        if (hov && ImGui::IsMouseDoubleClicked(0) && onOpenSprite) onOpenSprite(jstr(*sp, "id"));
        ui::checkerboard(dl, p, ImVec2(p.x + cell, p.y + cell), 6);
        if (ui::PixTexture *tx = spriteFrameTex(proj, jstr(*sp, "id"), i)) {
            float sc = (cell - 8) / std::max(tx->w, tx->h), w = tx->w * sc, h = tx->h * sc;
            dl->AddImage(tx->id(), ImVec2(p.x + (cell - w) / 2, p.y + (cell - h) / 2), ImVec2(p.x + (cell + w) / 2, p.y + (cell + h) / 2));
        }
        dl->AddRect(p, ImVec2(p.x + cell, p.y + cell), hov ? ui::kPrimary : IM_COL32(255, 255, 255, 40), 4, 0, hov ? 2.0f : 1.0f);
        char num[16]; snprintf(num, sizeof num, "%d", i + 1);
        dl->AddText(ImVec2(p.x + cell / 2 - ImGui::CalcTextSize(num).x / 2, p.y + cell + 2), ui::kDim, num);
        if (hov && n > 1) {   // x para borrar de una
            ImVec2 xa(p.x + cell - 18, p.y + 2), xb(p.x + cell - 2, p.y + 18);
            bool on = ImGui::IsMouseHoveringRect(xa, xb);
            dl->AddRectFilled(xa, xb, on ? IM_COL32(240, 96, 93, 255) : IM_COL32(40, 40, 48, 230), 4);
            dl->AddLine(ImVec2(xa.x + 4, xa.y + 4), ImVec2(xb.x - 4, xb.y - 4), IM_COL32_WHITE, 1.5f);
            dl->AddLine(ImVec2(xb.x - 4, xa.y + 4), ImVec2(xa.x + 4, xb.y - 4), IM_COL32_WHITE, 1.5f);
            if (on && ImGui::IsMouseReleased(0) && !ImGui::GetDragDropPayload()) del = i;
            ImGui::SetTooltip("Arrastra para cambiar el orden. Clic derecho: más opciones. Doble clic: dibujar");
        }
        ImGui::PopID();
    }
    if (n) ImGui::SameLine(0, gap);
    if (ImGui::Button("+##addf", ImVec2(36, cell))) objectFrameOp(*sp, 'n', 0, 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Añadir un frame vacío al final");
    ImGui::EndChild();
    bool changed = true;
    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) objectFrameOp(*sp, 'm', moveFrom, moveTo);
    else if (del >= 0 && n > 1) objectFrameOp(*sp, 'd', del, 0);
    else if (dup >= 0) objectFrameOp(*sp, 'c', dup, 0);
    else if ((int)(*sp)["frames"].size() == n) changed = false;
    if (changed) { proj.rev++; proj.dirty = true; if (onSpriteChanged) onSpriteChanged(jstr(*sp, "id")); }
}

// animacion nueva: la primera usa el sprite del objeto; las demas, un sprite nuevo del mismo tamano para dibujarla
void ObjectEditor::addAnimation(Project &proj, json &o) {
    size_t count = o["animations"].size();
    std::string name = count == 0 ? "Idle" : "Anim " + std::to_string(count + 1), sid = jstr(o, "spriteId");
    if (count || sid.empty() || !proj.spriteById(sid)) {
        json *base = proj.spriteById(sid);
        int w = base ? (int)jnum(*base, "width", 32) : 32, h = base ? (int)jnum(*base, "height", 32) : 32;
        sid = onNewSprite ? onNewSprite(jstr(o, "name") + " " + name, w, h) : "";
        if (jstr(o, "spriteId").empty()) o["spriteId"] = sid;
    }
    json &anims = o["animations"];   // despues de tocar el objeto (una clave nueva lo mueve en memoria)
    anims.push_back({{"id", makeUid("a")}, {"name", name}, {"spriteId", sid}, {"fps", 8}, {"loop", true}});
    curAnim = (int)anims.size() - 1;
    proj.rev++; proj.dirty = true;
}

void ObjectEditor::tabAnims(Project &proj, json &o) {
    // todas las claves que se usan aqui, antes de coger referencias: anadir una clave al objeto mueve las demas en
    // memoria y dejaria "anims" apuntando a nada (asi se caia el editor)
    if (!o.contains("animations") || !o["animations"].is_array()) o["animations"] = json::array();
    if (!o.contains("collision")) o["collision"] = nullptr;
    if (!o.contains("animSlots") || !o["animSlots"].is_object()) o["animSlots"] = json::object();
    if (!o.contains("flipOnMove")) o["flipOnMove"] = true;
    if (!o.contains("spriteId")) o["spriteId"] = nullptr;
    json &anims = o["animations"];
    editBool("Voltear al moverse hacia la izquierda", o, "flipOnMove", true);
    ImGui::Spacing();
    if (anims.empty()) {
        ImGui::TextWrapped("Este objeto todavía no tiene animaciones. Cada animación es una tira de dibujos que se van pasando "
                           "(quieto, caminar, saltar...). La primera usa el dibujo que ya tiene el objeto.");
        ImGui::Spacing();
    }
    int del = -1;
    for (int i = 0; i < (int)anims.size(); i++) {   // una tarjeta por animacion: sus datos y sus frames
        json &a = anims[i];
        ImGui::PushID(i);
        bool cur = i == curAnim;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, cur ? ImVec4(124 / 255.f, 92 / 255.f, 255 / 255.f, 0.10f) : ImVec4(44 / 255.f, 44 / 255.f, 55 / 255.f, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, cur ? ImGui::ColorConvertU32ToFloat4(ui::kPrimary) : ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::BeginChild("##acard", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(0)) curAnim = i;
        ImGui::AlignTextToFramePadding();
        ImGui::SetNextItemWidth(170); editText("##an", a, "name");
        ImGui::SameLine(); ImGui::TextDisabled("Sprite"); ImGui::SameLine();
        ImGui::SetNextItemWidth(160); if (comboById("##as", a, "spriteId", proj.sprites())) proj.rev++;
        ImGui::SameLine(); ImGui::TextDisabled("FPS"); ImGui::SameLine();
        ImGui::SetNextItemWidth(60); editNum("##af", a, "fps", 8, 0.2f, "%.0f", 1, 60);
        ImGui::SameLine(); editBool("Bucle", a, "loop", true);
        ImGui::SameLine(); ImGui::SetNextItemWidth(150); editText("##aev", a, "events", "eventos: 3:paso");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Eventos de la animación (fotograma:nombre), por ejemplo 3:paso, 7:golpe");
        ImGui::SameLine();
        if (ImGui::SmallButton("Editar sprite") && onOpenSprite && !jstr(a, "spriteId").empty()) onOpenSprite(jstr(a, "spriteId"));
        ImGui::SameLine();
        if (ImGui::SmallButton("Quitar")) del = i;
        framesStrip(proj, a);
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (del >= 0) {
        std::string id = jstr(anims[del], "id");
        anims.erase(anims.begin() + del);
        if (o.contains("animSlots") && o["animSlots"].is_object())
            for (auto it = o["animSlots"].begin(); it != o["animSlots"].end();) { if (it.value() == id) it = o["animSlots"].erase(it); else ++it; }
    }
    if (ui::primaryButton("+ Añadir animación")) addAnimation(proj, o);
    ImGui::TextDisabled("Sin animaciones por estado, se usa la llamada Idle (quieto) o Walk (caminar).");
    if (curAnim >= (int)anims.size()) curAnim = (int)anims.size() - 1;
    if (curAnim < 0 && !anims.empty()) curAnim = 0;

    // colision: la de cada animacion (agachado, mas baja...) o, sin animaciones, la del objeto
    ImGui::SeparatorText("Colisión");
    school::anchor("obj.collision");
    ImGui::TextDisabled("Con qué parte del dibujo choca. Cada animación puede tener la suya: agachado, por ejemplo, más baja.");
    json *target = &o["collision"];
    std::string sid = jstr(o, "spriteId");
    if (!anims.empty()) {
        labelRow("Animación");
        if (ImGui::BeginCombo("##colanim", jstr(anims[curAnim], "name").c_str())) {
            for (int i = 0; i < (int)anims.size(); i++) if (ImGui::Selectable((jstr(anims[i], "name") + "##ca" + std::to_string(i)).c_str(), i == curAnim)) curAnim = i;
            ImGui::EndCombo();
        }
        json &ca = anims[curAnim];
        if (!ca.contains("collision")) ca["collision"] = nullptr;
        target = &ca["collision"];
        sid = jstr(ca, "spriteId");
    }
    json *sp = proj.spriteById(sid);
    int sw = sp ? (int)jnum(*sp, "width", 32) : 32, sh = sp ? (int)jnum(*sp, "height", 32) : 32;
    int nf = sp && (*sp)["frames"].is_array() ? (int)(*sp)["frames"].size() : 1;
    if (nf > 1) { labelRow("Frame de referencia"); ImGui::SliderInt("##colframe", &colFrame, 1, nf); }
    colFrame = std::clamp(colFrame, 1, std::max(1, nf));
    if (collisionEditor("##colmask", *target, sp ? spriteFrameTex(proj, sid, colFrame - 1) : nullptr, sw, sh, 300)) { proj.dirty = true; proj.rev++; }
    if (anims.size() > 1) {
        if (ImGui::Button("Aplicar a todas las animaciones")) {
            json m = *target;
            for (auto &a : anims) a["collision"] = m;
            proj.dirty = true;
            if (onStatus) onStatus("La colisión de " + jstr(anims[curAnim], "name") + " se puso en todas las animaciones");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copia esta forma a las demás animaciones del objeto");
    }

    auto types = objBehaviorTypes(o);
    bool plat = std::find(types.begin(), types.end(), "platformer") != types.end();
    bool top = std::find(types.begin(), types.end(), "topdown") != types.end();
    if (!plat && !top) return;
    ImGui::SeparatorText("Animaciones por estado");
    json cp = o.contains("comportamientos") ? o["comportamientos"] : json::object();
    json pf = cp.contains("plataformero") ? cp["plataformero"] : json::object();
    std::vector<std::pair<const char *, const char *>> slots = {{"quieto", "Quieto"}, {"caminar", "Caminar"}};
    if (plat) {
        if (jbool(pf, "run", false)) slots.push_back({"correr", "Correr"});
        slots.push_back({"saltar", "Saltar"}); slots.push_back({"caer", "Caer"}); slots.push_back({"escalera", "Subir escaleras"});
        if (jbool(pf, "dash", false)) slots.push_back({"dash", "Dash"});
        if (jbool(pf, "crouch", false)) slots.push_back({"agachado", "Agachado"});
        if (jbool(pf, "wall", false)) slots.push_back({"pared", "Pegado en pared"});
    }
    json &as = sub(o, "animSlots");
    for (auto &s : slots) {
        labelRow(s.second);
        std::string cur = jstr(as, s.first), curName = "(automático)";
        for (auto &a : anims) if (jstr(a, "id") == cur) curName = jstr(a, "name");
        ImGui::PushID(s.first);
        if (ImGui::BeginCombo("##slot", curName.c_str())) {
            if (ImGui::Selectable("(automático)", cur.empty())) as.erase(s.first);
            for (auto &a : anims) if (ImGui::Selectable(jstr(a, "name").c_str(), jstr(a, "id") == cur)) as[s.first] = jstr(a, "id");
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
}

void ObjectEditor::tabBehaviors(Project &, json &o) {
    auto types = objBehaviorTypes(o);
    if (types.empty()) ImGui::TextWrapped("Este objeto no tiene comportamientos. Los comportamientos le dan movimiento y reglas listas para usar, sin eventos.");
    std::string remove;
    json &b = sub(o, "behaviors");
    for (auto &t : types) {
        const BhInfo *info = bhInfo(t);
        ImGui::PushID(t.c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(44 / 255.f, 44 / 255.f, 55 / 255.f, 1));
        ImGui::BeginChild("##card", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextUnformatted(info ? info->name : t.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
        if (ImGui::SmallButton("Quitar")) remove = t;
        ImGui::TextDisabled("%s", info ? info->desc : "");
        ImGui::Separator();
        if (t == "platformer") {
            json &cp = sub(o, "comportamientos");
            json pf = cp.contains("plataformero") ? cp["plataformero"] : json::object();
            json before = pf;
            ImGui::TextDisabled("Teclas (vacío = flechas, A/D y espacio):");
            labelRow("Izquierda"); editText("##kl", pf, "keyLeft", "arrowleft");
            labelRow("Derecha"); editText("##kr", pf, "keyRight", "arrowright");
            labelRow("Saltar"); editText("##kj", pf, "keyJump", "space");
            labelRow("Agacharse / bajar"); editText("##kd", pf, "keyDown", "s");
            labelRow("Rapidez"); editNum("##spd", b, "speed", 150, 1, "%.0f", 0, 5000);
            labelRow("Capacidad de salto"); editNum("##jmp", b, "jump", 620, 1, "%.0f", 0, 5000);
            editBool("Puede correr", pf, "run", false);
            if (jbool(pf, "run", false)) {
                labelRow("Rapidez al correr"); editNum("##rs", pf, "runSpeed", 0, 1, "%.0f (0 = automática)", 0, 5000);
                labelRow("Tecla de correr"); editText("##krun", pf, "keyRun", "shift");
            }
            editBool("Puede agacharse", pf, "crouch", false); ImGui::SameLine(200);
            editBool("Se desliza y pega en paredes", pf, "wall", false);
            if (jbool(pf, "wall", false)) { ImGui::SameLine(); editBool("Salto de pared (trepar)", pf, "wallJump", false); }
            labelRow("Rapidez en escaleras"); editNum("##cl", pf, "climb", 0, 1, "%.0f (0 = automática)", 0, 5000);
            ImGui::SeparatorText("Saltos en el aire y dash");
            labelRow("Saltos en el aire"); editInt("##aj", pf, "airJumps", 0, 0, 10);
            if (jnum(pf, "airJumps", 0) > 0) { labelRow("Fuerza del doble salto"); editNum("##ajf", pf, "airJump", 0, 1, "%.0f (0 = la del salto)", 0, 5000); }
            editBool("Dash", pf, "dash", false);
            if (jbool(pf, "dash", false)) {
                labelRow("Tecla del dash"); editText("##kdash", pf, "keyDash", "shift");
                labelRow("Velocidad del dash"); editNum("##dsp", pf, "dashSpeed", 720, 1, "%.0f px/s", 0, 10000);
                labelRow("Duración del dash"); editNum("##dt", pf, "dashTime", 0.16, 0.005f, "%.2f s", 0.01, 2);
                labelRow("Espera entre dashes"); editNum("##dcd", pf, "dashCooldown", 0.35, 0.01f, "%.2f s", 0, 10);
                ImGui::TextDisabled("En el aire, un dash hasta volver a tocar el suelo. Animación por estado: Dash.");
            }
            ImGui::SeparatorText("Control fino");
            editBool("Salto variable (soltar el salto lo corta)", pf, "jumpCut", false);
            labelRow("Tiempo de coyote"); editNum("##coy", pf, "coyote", 0, 0.005f, "%.2f s", 0, 1);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Se puede saltar un momento después de salir del borde");
            labelRow("Búfer de salto"); editNum("##jb", pf, "jumpBuffer", 0, 0.005f, "%.2f s", 0, 1);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Un salto pulsado un poco antes de aterrizar se hace al tocar el suelo");
            labelRow("Caída máxima"); editNum("##mf", pf, "maxFall", 0, 5, "%.0f px/s (0 = sin límite)", 0, 10000);
            if (pf != before) cp["plataformero"] = pf;     // los antiguos crean sus opciones solo al editarlas
        } else if (t == "topdown") {
            labelRow("Rapidez"); editNum("##spd", b, "speed", 150, 1, "%.0f", 0, 5000);
            labelRow("Tamaño de casilla"); editNum("##cell", b, "cell", 16, 1, "%.0f px", 1, 512);
            editBool("Movimiento por casillas", b, "grid", false); ImGui::SameLine(240);
            editBool("Pixel perfect (redondea la posición)", b, "pixel", false);
        } else if (t == "rts") {
            labelRow("Rapidez"); editNum("##spd", b, "speed", 150, 1, "%.0f", 0, 5000);
            ImGui::TextWrapped("Clic izquierdo selecciona (o arrastra un recuadro). Clic derecho la manda a ese punto.");
        } else if (t == "platform") {
            std::string cur = platType(b);
            labelRow("Tipo");
            const char *names[] = {"Sólida", "Traspasable", "Escalera"}, *keys[] = {"solid", "oneway", "ladder"};
            int idx = cur == "oneway" ? 1 : cur == "ladder" ? 2 : 0;
            if (ImGui::Combo("##ptype", &idx, names, 3)) applyPlatType(b, keys[idx]);
            ImGui::TextDisabled("Traspasable: se atraviesa desde abajo. Escalera: arriba y abajo para subir y bajar.");
        } else if (t == "movingPlatform") {
            json &pl = sub(sub(o, "comportamientos"), "plataforma");
            labelRow("Mover X"); editNum("##dx", pl, "dx", 0, 1, "%.0f px", -10000, 10000);
            labelRow("Mover Y"); editNum("##dy", pl, "dy", 0, 1, "%.0f px", -10000, 10000);
            labelRow("Velocidad"); editNum("##psp", pl, "speed", 60, 1, "%.0f px/s", 0, 5000);
        } else if (info && o.contains("behaviorList")) {
            json *cfg = nullptr;
            for (auto &x : o["behaviorList"]) if (jstr(x, "type") == t) cfg = &x;
            if (cfg && info->props.empty()) ImGui::TextDisabled("Sin propiedades. Sus condiciones y acciones aparecen al elegir este objeto en los eventos.");
            if (cfg) for (auto &p : info->props) {
                labelRow(p.label);
                std::string id = std::string("##") + p.k;
                if (std::string(p.type) == "select") comboOptions(id.c_str(), *cfg, p.k, p.opts, p.sdef);
                else if (std::string(p.type) == "text") editText(id.c_str(), *cfg, p.k, std::string(p.k) == "start" ? "vacío = la salud máxima" : nullptr);
                else editNum(id.c_str(), *cfg, p.k, p.d, 0.1f, "%g");
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (!remove.empty()) bhRemove(o, remove);
    bool addBh = ui::primaryButton("+ Añadir un comportamiento");
    school::anchor("bh.add");
    if (addBh) ImGui::OpenPopup("Añadir un comportamiento");
    ImGui::SetNextWindowSize(ImVec2(720, 0), ImGuiCond_Appearing);
    if (ui::beginModal("Añadir un comportamiento", nullptr)) {
        ImGui::TextDisabled("Los de movimiento (plataformero, cenital, estrategia) se sustituyen entre si.");
        int i = 0;
        for (auto &c : catalog()) {
            bool has = std::find(types.begin(), types.end(), c.type) != types.end();
            if (i++ % 2) ImGui::SameLine();
            ImGui::BeginDisabled(has);
            ImGui::PushID(c.type);
            std::string label = std::string(c.name) + (has ? " (ya lo tiene)" : "") + "\n";
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool pick = ImGui::Button("##pick", ImVec2(340, 64));
            school::anchor((std::string("bh.") + c.type).c_str());
            if (pick) { bhAdd(o, c.type); ImGui::CloseCurrentPopup(); }
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(p.x + 10, p.y + 8), IM_COL32(235, 235, 242, 255), label.c_str());
            dl->AddText(ImGui::GetFont(), 13, ImVec2(p.x + 10, p.y + 28), ui::kDim, c.desc, nullptr, 320);
            ImGui::PopID();
            ImGui::EndDisabled();
        }
        ImGui::Separator();
        if (ImGui::Button("Cerrar", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ObjectEditor::tabVars(json &o) {
    if (!o.contains("vars") || !o["vars"].is_array()) o["vars"] = json::array();
    json &vars = o["vars"];
    ImGui::TextWrapped("Cada instancia empieza con estas variables. En los eventos: condición \"Variable del objeto\" y acciones para cambiarlas.");
    int del = -1;
    if (ImGui::BeginTable("##vars", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Nombre"); ImGui::TableSetupColumn("Valor inicial"); ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)vars.size(); i++) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); editText("##vn", vars[i], "name");
            ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
            {
                std::string v = jstr(vars[i], "value");
                char buf[256]; snprintf(buf, sizeof buf, "%s", v.c_str());
                if (ImGui::InputText("##vv", buf, sizeof buf)) {
                    char *e = nullptr; double d = strtod(buf, &e);
                    vars[i]["value"] = (buf[0] && e && *e == 0) ? json(d) : json(std::string(buf));
                }
            }
            ImGui::TableNextColumn(); if (ImGui::SmallButton("Quitar")) del = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (del >= 0) vars.erase(vars.begin() + del);
    if (ui::primaryButton("+ Añadir variable")) vars.push_back({{"name", "var" + std::to_string(vars.size() + 1)}, {"value", 0}});
}

void ObjectEditor::tabEffect(Project &proj, json &o) {
    static const std::vector<std::pair<std::string, std::string>> filters = {
        {"", "Sin efecto"}, {"grayscale(1)", "Escala de grises"}, {"sepia(0.8)", "Sepia"}, {"invert(1)", "Invertir colores"},
        {"blur(2px)", "Desenfoque"}, {"brightness(1.5)", "Brillo alto"}, {"contrast(1.8)", "Contraste"}, {"saturate(2.2)", "Color intenso"},
        {"hue-rotate(90deg)", "Giro de tono"}, {"drop-shadow(0 0 6px #ffffff)", "Resplandor"}, {"opacity(0.5)", "Semi transparente"}};
    labelRow("Efecto");
    std::string cur = jstr(o, "shader");
    if (cur == "none") cur = "";
    std::string curName = cur.empty() ? "Sin efecto" : "Personalizado";
    for (auto &f : filters) if (f.first == cur) curName = f.second;
    if (ImGui::BeginCombo("##fx", curName.c_str())) {
        for (auto &f : filters) if (ImGui::Selectable(f.second.c_str(), f.first == cur)) o["shader"] = f.first;
        ImGui::EndCombo();
    }
    labelRow("Filtro personalizado"); editText("##fxc", o, "shader", "blur(3px) brightness(1.4)");
    ImGui::TextDisabled("Combina: blur, brightness, contrast, saturate, hue-rotate, grayscale, sepia, invert, opacity, drop-shadow.");
    if (!proj.doc.contains("shaders")) proj.doc["shaders"] = json::array();
    labelRow("Shader GLSL"); comboById("##shid", o, "shaderId", proj.doc["shaders"], "(ninguno)");
    // contorno como el efecto Outline de GDevelop (se ve en la escena y en el juego)
    if (!o.contains("outline") || !o["outline"].is_object()) o["outline"] = {{"width", 0}, {"color", "#000000"}};
    labelRow("Contorno (px)"); editNum("##olw", o["outline"], "width", 0, 0.1f, "%.1f", 0, 8);
    labelRow("Color del contorno"); editColor("##olc", o["outline"], "color", "#000000");
}

void ObjectEditor::draw(Project &proj) {
    json *op = findById(proj.doc["objects"], objectId);
    if (!op) { open = false; return; }
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(820, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title(proj).c_str(), &open)) { ImGui::End(); hist.track(*op); return; }
    drawBody(proj);
    ImGui::End();
}

// el contenido (cabecera y pestanas); tambien dentro del editor de escena
void ObjectEditor::drawBody(Project &proj) {
    json *op = findById(proj.doc["objects"], objectId);
    if (!op) { open = false; return; }
    json &o = *op;
    json before = o;
    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ui::noteFocus("object", objectId);
    if (int h = ui::takeHistoryRequest()) { if (h > 0 ? hist.doRedo(o) : hist.doUndo(o)) proj.dirty = true; }
    if (focused && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (ImGui::GetIO().KeyShift ? hist.doRedo(o) : hist.doUndo(o)) proj.dirty = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && hist.doRedo(o)) proj.dirty = true;
    }
    ImVec2 tp = ImGui::GetCursorPos();
    objectThumb(proj, o, 48);
    ImGui::SetCursorPos(ImVec2(tp.x + 58, tp.y + 4));
    ImGui::PushFont(ui::boldFont(), 20.0f); ImGui::TextUnformatted(jstr(o, "name").c_str()); ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(tp.x + 58, tp.y + 30));
    std::string type = jstr(o, "type", "sprite");
    ImGui::TextDisabled("%s", objTypeLabel(type));
    ImGui::SetCursorPos(ImVec2(tp.x, tp.y + 56));
    if (nextTab >= 0) { forceTab = nextTab; nextTab = -1; }   // pedido desde una pestana (se aplica al fotograma siguiente)
    if (ImGui::BeginTabBar("##otabs")) {
        auto fl = [&](int i) { return forceTab == i ? ImGuiTabItemFlags_SetSelected : 0; };
        if (ImGui::BeginTabItem("Propiedades", nullptr, fl(0))) { ImGui::BeginChild("##p"); tabProps(proj, o); ImGui::EndChild(); ImGui::EndTabItem(); }
        if (type == "sprite" && ImGui::BeginTabItem("Animaciones", nullptr, fl(1))) { ImGui::BeginChild("##a"); tabAnims(proj, o); ImGui::EndChild(); ImGui::EndTabItem(); }
        bool bhTab = ImGui::BeginTabItem("Comportamientos", nullptr, fl(2));
        school::anchor("obj.tab.bh");
        if (bhTab) { ImGui::BeginChild("##b"); tabBehaviors(proj, o); ImGui::EndChild(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Variables", nullptr, fl(3))) { ImGui::BeginChild("##v"); tabVars(o); ImGui::EndChild(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Efecto", nullptr, fl(4))) { ImGui::BeginChild("##e"); tabEffect(proj, o); ImGui::EndChild(); ImGui::EndTabItem(); }
        forceTab = -1;
        ImGui::EndTabBar();
    }
    if (o != before) proj.dirty = true;
    hist.track(o);
}
