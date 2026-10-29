#include "school.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include "assets.hpp"
#include "ui.hpp"
#include "imgui_internal.h"
#include "presets.h"
#include "object_editor.hpp"
#include "tiles_obj.hpp"
#include <cmath>
#include <map>

namespace school {
std::function<void(const std::string &name)> onNewProject;
std::function<void(const std::string &msg)> onNotify;
std::function<void()> onAddSong;

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

static bool tileCells(const json &d, int n) {   // algun objeto Tile con al menos n celdas pintadas en una escena
    for (auto &o : arrOf(d, "objects")) if (jstr(o, "type") == "tile")
        for (auto &sc : arrOf(d, "scenes")) for (auto &i : arrOf(sc, "instances")) {
            if (jstr(i, "objectId") != jstr(o, "id") || !i.contains("tm")) continue;
            int k = 0;
            for (auto &v : arrOf(i["tm"], "data")) k += v.is_number() && v.get<int>() >= 0;
            if (k >= n) return true;
        }
    return false;
}

/* ---------------- "hazlo por mi": lo que hace Floppa cuando se lo pides ---------------- */
static json *firstScene(Project &proj) { return proj.doc["scenes"].is_array() && !proj.doc["scenes"].empty() ? &proj.doc["scenes"][0] : nullptr; }
static json *objBy(Project &proj, const std::function<bool(const json &)> &f) {
    for (auto &o : proj.doc["objects"]) if (f(o)) return &o;
    return nullptr;
}
static json *newestSprite(Project &proj) {   // el ultimo objeto Sprite creado
    json *r = nullptr;
    for (auto &o : proj.doc["objects"]) if (isSprite(o)) r = &o;
    return r;
}
static void placeIn(Project &proj, const std::string &objId, float x, float y) {
    if (json *sc = firstScene(proj))
        (*sc)["instances"].push_back({{"id", makeUid("i")}, {"objectId", objId}, {"x", x}, {"y", y}, {"angle", 0}, {"scaleX", 1}, {"scaleY", 1}, {"opacity", 100}, {"frame", 0}});
    proj.dirty = true; proj.rev++;
}
// un dibujo sencillo en el sprite del objeto (kind 0: personaje, 1: moneda)
static void paintSimple(Project &proj, json &o, int kind) {
    json *sp = proj.spriteById(jstr(o, "spriteId"));
    if (!sp) {
        std::string sid = makeUid("s");
        json px = json::array();
        for (int i = 0; i < 32 * 32; i++) px.push_back("");
        proj.sprites().push_back({{"id", sid}, {"name", jstr(o, "name")}, {"width", 32}, {"height", 32}, {"frames", json::array({{{"pixels", px}}})}});
        o["spriteId"] = sid;
        sp = proj.spriteById(sid);
    }
    int w = (int)jnum(*sp, "width", 32), h = (int)jnum(*sp, "height", 32);
    json px = json::array();
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        std::string c;
        if (kind == 0) {   // cuadrado azul con ojos
            bool in = x >= w / 8 && x < w - w / 8 && y >= h / 8 && y < h;
            bool eye = (y >= h / 3 && y < h / 3 + h / 8) && ((x >= w / 3 && x < w / 3 + w / 10) || (x >= 2 * w / 3 - w / 10 && x < 2 * w / 3));
            c = eye ? "#ffffff" : in ? (y > h * 3 / 4 ? "#2f5fd0" : "#4c86f0") : "";
        } else {           // moneda amarilla
            float dx = x - w / 2.0f + 0.5f, dy = y - h / 2.0f + 0.5f, r = w * 0.38f;
            c = dx * dx + dy * dy <= r * r ? (dx * dx + dy * dy <= r * r * 0.45f ? "#ffe066" : "#e0a020") : "";
        }
        px.push_back(c);
    }
    (*sp)["frames"] = json::array({{{"pixels", px}}});
    sp->erase("layers");
    proj.dirty = true; proj.rev++;
}
static std::string makeSpriteObj(Project &proj, const std::string &name) {
    std::string id = createObject(proj, "sprite");
    if (json *o = findById(proj.doc["objects"], id)) {
        (*o)["name"] = name;
        if (json *sc = firstScene(proj)) (*o)["scene"] = jstr(*sc, "id");
        // su propio sprite (createObject reutiliza el primero del proyecto si ya hay)
        std::string sid = makeUid("s");
        json px = json::array();
        for (int i = 0; i < 32 * 32; i++) px.push_back("");
        proj.sprites().push_back({{"id", sid}, {"name", name}, {"width", 32}, {"height", 32}, {"frames", json::array({{{"pixels", px}}})}});
        (*o)["spriteId"] = sid;
    }
    return id;
}
static void paintFloor(Project &proj, json &tileObj) {
    json *sc = firstScene(proj);
    if (!sc) return;
    json *it = nullptr;
    for (auto &i : (*sc)["instances"]) if (jstr(i, "objectId") == jstr(tileObj, "id")) it = &i;
    if (!it) {
        (*sc)["instances"].push_back({{"id", makeUid("i")}, {"objectId", jstr(tileObj, "id")}, {"x", 0}, {"y", 0}, {"angle", 0}, {"scaleX", 1}, {"scaleY", 1},
                                      {"opacity", 100}, {"frame", 0}, {"tm", {{"cols", 0}, {"rows", 0}, {"data", json::array()}}}});
        it = &(*sc)["instances"].back();
    }
    float T = tileSizeOf(proj, tileObj), y = (float)jnum(*sc, "height", 540) - T * 3;
    for (float x = 0; x < 30 * T; x += T) tileInstPaint(proj, *it, tileObj, x + T / 2, y + T / 2, 0);
    proj.dirty = true; proj.rev++;
}
// lo de la leccion Principal ya hecho: personaje dibujado, con plataformero y puesto, y un suelo (para las demas)
static void ensurePlatformer(Project &proj) {
    json *pl = objBy(proj, [](const json &o) { return isSprite(o) && platformer(o); });
    if (!pl) {
        std::string id = makeSpriteObj(proj, "Jugador");
        pl = findById(proj.doc["objects"], id);
        objectAddBehavior(*pl, "platformer");
    }
    if (!painted(proj.doc, jstr(*pl, "spriteId"))) paintSimple(proj, *pl, 0);
    if (!instancesOf(proj.doc, jstr(*pl, "id"))) placeIn(proj, jstr(*pl, "id"), 160, 300);
    json *fl = objBy(proj, [](const json &o) { return jstr(o, "type") == "tile"; });
    if (!fl) {
        std::string id = createObject(proj, "tile");
        fl = findById(proj.doc["objects"], id);
        if (json *sc = firstScene(proj)) (*fl)["scene"] = jstr(*sc, "id");
    }
    if (!tileCells(proj.doc, 6)) paintFloor(proj, *fl);
}
static void addEventTo(Project &proj, json conds, json acts) {
    json *sc = firstScene(proj);
    if (!sc) return;
    if (!sc->contains("events") || !(*sc)["events"].is_array()) (*sc)["events"] = json::array();
    auto wrap = [](json list) { for (auto &x : list) { x["enabled"] = true; if (!x.contains("invert")) x["invert"] = false; } return list; };
    (*sc)["events"].push_back({{"id", makeUid("e")}, {"conditions", wrap(conds)}, {"actions", wrap(acts)}});
    proj.dirty = true; proj.rev++;
}
static std::string objIdNamed(Project &proj, const std::string &lowerPart) {
    for (auto &o : proj.doc["objects"]) {
        std::string n = jstr(o, "name");
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (n.find(lowerPart) != std::string::npos) return jstr(o, "id");
    }
    return "";
}
static bool hasGlobal(const json &d, const char *name) { for (auto &g : arrOf(d, "globals")) if (jstr(g, "name") == name) return true; return false; }
static bool namedObj(const json &d, const char *lowerPart, const char *type = "sprite") {
    for (auto &o : arrOf(d, "objects")) {
        std::string n = jstr(o, "name");
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (jstr(o, "type", "sprite") == type && n.find(lowerPart) != std::string::npos) return true;
    }
    return false;
}
static int instancesNamed(const json &d, const char *lowerPart) {
    int k = 0;
    for (auto &o : arrOf(d, "objects")) {
        std::string n = jstr(o, "name");
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (n.find(lowerPart) != std::string::npos) k += instancesOf(d, jstr(o, "id"));
    }
    return k;
}

/* ---------------- lecciones ---------------- */
// Cada paso: lo que dice Floppa, una pista, cuando se cumple, a donde senala ("a|b": el primero que se vea), si espera
// "Estoy listo", que decir si no se ve lo que senala (te perdiste: como volver y a donde senalar) y "Hazlo por mi".
struct Step {
    const char *text, *hint;
    std::function<bool(const json &)> check;
    const char *target = nullptr;
    bool ready = false;
    const char *readyLabel = nullptr;
    const char *lost = nullptr, *lostTarget = nullptr;
    std::function<void(Project &)> autoDo;
    Step(const char *t, const char *h = "") : text(t), hint(h) {}
    Step &at(const char *t) { target = t; return *this; }
    Step &ok(std::function<bool(const json &)> f) { check = std::move(f); return *this; }
    Step &next(const char *label = "Siguiente") { ready = true; readyLabel = label; return *this; }
    Step &whenLost(const char *how, const char *where) { lost = how; lostTarget = where; return *this; }
    Step &doIt(std::function<void(Project &)> f) { autoDo = std::move(f); return *this; }
};
struct Lesson { const char *id, *title, *desc, *group; std::vector<Step> steps; std::function<void(Project &)> setup; };
static bool noted(const char *k) { return g_notes.count(k) > 0; }
static const char *kToScene = "Pulsa la pestaña de la escena (arriba, la del dibujo de una imagen). Si no está, abre el panel ☰ y en Escenas pulsa tu escena.";
static const std::vector<Lesson> &lessons() {
    static std::vector<Lesson> L = {
        {"editor", "Conoce el editor", "Para qué sirve cada parte: barra, pestañas, panel, escena, eventos y opciones.", "Empieza aquí", {
            Step("¡Hola! Soy Floppa. Antes de hacer juegos, te enseño el editor. Cada vez que te señale algo, míralo y pulsa \"Siguiente\". ¡Es fácil!").next("¡Vamos!"),
            Step("Este es el botón de guardar. El editor también guarda solo cada poco, pero si quieres estar seguro, púlsalo (o Ctrl+S).",
                 "El puntito morado en el disquete quiere decir que hay cambios sin guardar.").at("toolbar.save").next(),
            Step("Deshacer y rehacer. Si te equivocas, deshaces (Ctrl+Z). Si te arrepientes, rehaces (Ctrl+Y).").at("toolbar.undo").next(),
            Step("Trabajar juntos: invita a un amigo para hacer el juego a la vez, cada uno desde su ordenador.").at("toolbar.collab").next(),
            Step("Vista previa: prueba tu juego en una ventana. Púlsala ahora (aunque el juego esté vacío) y luego cierra esa ventana.",
                 "La ventana del juego se cierra con la X o con Escape.").at("toolbar.preview").ok([](const json &) { return noted("preview"); })
                .whenLost(kToScene, "tab.scene|menu.proyecto"),
            Step("Exportar: cuando tu juego esté listo, aquí lo conviertes en un programa para Windows, Linux o la web.").at("toolbar.export").next(),
            Step("Estas son las pestañas. Cada cosa que abres (la escena, los eventos, un objeto, un sprite) sale aquí. Pulsa una para ir a ella.",
                 "La X de cada pestaña la cierra; lo que hay dentro no se borra.").at("tabs").next(),
            Step("Ahora abre el panel del proyecto: el botón de las tres rayas ☰.", "El panel se cierra solo al hacer clic fuera de él.")
                .at("menu.proyecto").ok([](const json &) { return noted("panel"); }),
            Step("En el panel está todo tu proyecto: sprites (los dibujos), escenas (los niveles), objetos, eventos, música y ajustes. Míralo y pulsa Siguiente.",
                 "Para cerrarlo, haz clic fuera.").at("panel|menu.proyecto").next(),
            Step("Esta es la escena: el nivel de tu juego. Aquí pones a los personajes, las monedas y el suelo. Con la rueda del ratón haces zoom.",
                 "Clic con la rueda (o espacio + arrastrar) mueve la vista.").at("scene.canvas").next().whenLost(kToScene, "tab.scene|menu.proyecto"),
            Step("A la izquierda, la lista de objetos de la escena. Para poner uno en el nivel, mantenlo pulsado y arrástralo a la escena.",
                 "Doble clic en un objeto: lo editas.").at("scene.objlist").next().whenLost(kToScene, "tab.scene|menu.proyecto"),
            Step("Herramientas de la escena: seleccionar y mover, colocar muchas veces, pintar tiles, y la cuadrícula. Sin cuadrícula, los objetos se mueven libres.")
                .at("scene.modes").next().whenLost(kToScene, "tab.scene|menu.proyecto"),
            Step("A la derecha están las propiedades: lo que tengas elegido (la escena, un objeto puesto, las capas) se cambia aquí.",
                 "Elige un objeto puesto en la escena y aquí verás su posición, tamaño y giro.").at("scene.props").next().whenLost(kToScene, "tab.scene|menu.proyecto"),
            Step("Ahora pulsa la pestaña Eventos: ahí se escriben las reglas del juego.").at("tab.events").ok([](const json &) { return noted("events"); })
                .whenLost("Abre el panel ☰ y en Eventos pulsa los de tu escena.", "menu.proyecto"),
            Step("Un evento es una regla: \"cuando pase esto (condiciones, a la izquierda), haz aquello (acciones, a la derecha)\". Con este botón añades uno.")
                .at("events.add").next().whenLost("Vuelve a la pestaña Eventos.", "tab.events|menu.proyecto"),
            Step("En el menú Archivo están Abrir, Guardar, Exportar y las Opciones: el idioma, el guardado automático y las animaciones del editor.",
                 "Ábrelo si quieres curiosear y luego pulsa Siguiente.").at("menu.archivo").next(),
            Step("Y la casita te lleva al Inicio: tus proyectos, los juegos de ejemplo, la Escuela, Source Sprite y Source Sound.").at("tab.home").next(),
            Step("¡Ya conoces el editor! Ahora sigue con la lección Principal y haz tu primer juego.").next("¡Gracias, Floppa!")}, nullptr},

        {"principal", "Principal", "Un juego de plataformas de principio a fin, con Floppa.", "Plataformero", {
            Step("¡Hola! Soy Floppa. Hoy vamos a hacer un juego de plataformas juntos. Yo te voy señalando dónde pulsar, ¿vale?").next("¡Vamos!"),
            Step("Primero, tu personaje. Pulsa \"+ Añadir un objeto\" y elige Sprite.",
                 "Un objeto es cualquier cosa del juego: el personaje, una moneda, un enemigo.")
                .ok([](const json &d) { for (auto &o : arrOf(d, "objects")) if (isSprite(o)) return true; return false; })
                .at("newobj.sprite|scene.addobj").whenLost(kToScene, "tab.scene|menu.proyecto")
                .doIt([](Project &p) { makeSpriteObj(p, "Jugador"); }),
            Step("¡Ya tienes un objeto! Ahora vamos a dibujarlo: pulsa \"Editar sprite\".",
                 "El sprite es el dibujo del objeto. Puede tener varios frames para animarlo.")
                .ok([](const json &d) { if (noted("sprite")) return true; for (auto &o : arrOf(d, "objects")) if (isSprite(o) && painted(d, jstr(o, "spriteId"))) return true; return false; })
                .at("obj.editsprite").whenLost("Haz doble clic en tu personaje en la lista de objetos de la escena: se abre su editor.", "scene.objlist|tab.scene|menu.proyecto")
                .doIt([](Project &p) { if (json *o = newestSprite(p)) paintSimple(p, *o, 0); }),
            Step("Dibuja tu personaje con el lápiz: clic izquierdo pinta, clic derecho borra. Tómate tu tiempo; cuando termines, dime que estás listo.",
                 "Con la rueda del ratón haces zoom. Ctrl+Z deshace.")
                .ok([](const json &d) { for (auto &o : arrOf(d, "objects")) if (isSprite(o) && painted(d, jstr(o, "spriteId"))) return true; return false; })
                .at("sprite.pencil").next("Estoy listo").whenLost("Vuelve a la pestaña del sprite (arriba) o pulsa Editar sprite en tu objeto.", "obj.editsprite|scene.objlist|menu.proyecto")
                .doIt([](Project &p) { if (json *o = newestSprite(p)) paintSimple(p, *o, 0); }),
            Step("¡Qué bonito! Ahora ponlo en el nivel: vuelve a la escena, mantén pulsado tu personaje en la lista y arrástralo hasta el nivel.",
                 "Cada vez que lo pones es una instancia: puedes tener muchas del mismo objeto.")
                .ok([](const json &d) { for (auto &o : arrOf(d, "objects")) if (isSprite(o) && instancesOf(d, jstr(o, "id"))) return true; return false; })
                .at("scene.objlist").whenLost(kToScene, "tab.scene|scene.back|menu.proyecto")
                .doIt([](Project &p) { if (json *o = newestSprite(p)) placeIn(p, jstr(*o, "id"), 160, 300); }),
            Step("Ahora que salte: haz doble clic en tu personaje en la lista, ve a Comportamientos y añade \"Personaje plataformero\".",
                 "Los comportamientos le dan al objeto una forma de moverse sin escribir eventos.")
                .ok([](const json &d) { for (auto &o : arrOf(d, "objects")) if (platformer(o)) return true; return false; })
                .at("bh.platformer|bh.add|obj.tab.bh|scene.objlist").whenLost(kToScene, "tab.scene|menu.proyecto")
                .doIt([](Project &p) { if (json *o = newestSprite(p)) objectAddBehavior(*o, "platformer"); p.dirty = true; p.rev++; }),
            Step("Le falta un suelo. Pulsa \"+ Añadir un objeto\" y elige Tile.",
                 "Un Tile es un mapa de cuadraditos que pintas en la escena; crece solo según pintas.")
                .ok([](const json &d) { for (auto &o : arrOf(d, "objects")) if (jstr(o, "type") == "tile") return true; return false; })
                .at("newobj.tile|scene.addobj").whenLost(kToScene, "tab.scene|scene.back|menu.proyecto")
                .doIt([](Project &p) { std::string id = createObject(p, "tile"); if (json *o = findById(p.doc["objects"], id)) if (json *sc = firstScene(p)) (*o)["scene"] = jstr(*sc, "id"); }),
            Step("Pinta el suelo debajo de tu personaje: elige el Tile en la lista y mantén el clic izquierdo mientras arrastras por la escena. El cuadradito rojo del tile quiere decir que es sólido.",
                 "Clic derecho borra. Si te sales, el mapa se agranda solo.")
                .ok([](const json &d) { return tileCells(d, 6); }).at("scene.canvas").whenLost(kToScene, "tab.scene|scene.back|menu.proyecto")
                .doIt([](Project &p) { if (json *o = objBy(p, [](const json &x) { return jstr(x, "type") == "tile"; })) paintFloor(p, *o); }),
            Step("¡Hora de jugar! Pulsa Vista previa. Flechas para moverte y espacio para saltar.",
                 "Si el personaje cae al vacío, pinta el suelo justo debajo de él.")
                .ok([](const json &) { return noted("preview"); }).at("toolbar.preview"),
            Step("¡Lo lograste! Ya hiciste tu primer juego de plataformas. Sigue con \"Monedas y puntos\" en la Escuela.").next("¡Gracias, Floppa!")}, nullptr},

        {"monedas", "Monedas y puntos", "Monedas que se recogen, una variable con los puntos y un marcador.", "Plataformero", {
            Step("¡Hola otra vez! Ya tienes un personaje que salta (si no, te lo he preparado yo). Ahora: monedas y puntos.").next("¡Vamos!"),
            Step("Crea la moneda: pulsa \"+ Añadir un objeto\" y elige Sprite.", "")
                .ok([](const json &d) { int k = 0; for (auto &o : arrOf(d, "objects")) k += isSprite(o) && !platformer(o); return k >= 1; })
                .at("newobj.sprite|scene.addobj").whenLost(kToScene, "tab.scene|scene.back|menu.proyecto")
                .doIt([](Project &p) { makeSpriteObj(p, "Moneda"); }),
            Step("Ponle de nombre Moneda: escríbelo en Nombre, arriba de las propiedades del objeto.",
                 "El nombre sirve para encontrarlo en los eventos.")
                .ok([](const json &d) { return namedObj(d, "moneda"); }).at("obj.name")
                .whenLost("Haz doble clic en el objeto nuevo en la lista de la escena.", "scene.objlist|tab.scene|menu.proyecto")
                .doIt([](Project &p) { if (json *o = objBy(p, [](const json &x) { return isSprite(x) && !platformer(x); })) { (*o)["name"] = "Moneda"; p.dirty = true; p.rev++; } }),
            Step("Dibuja la moneda: pulsa Editar sprite y pinta un círculo amarillo. Cuando acabes, pulsa \"Estoy listo\".", "El cubo (G) rellena una zona entera de un color.")
                .ok([](const json &d) { for (auto &o : arrOf(d, "objects")) { std::string n = jstr(o, "name"); std::transform(n.begin(), n.end(), n.begin(), ::tolower); if (n.find("moneda") != std::string::npos && painted(d, jstr(o, "spriteId"))) return true; } return false; })
                .at("obj.editsprite|sprite.pencil").next("Estoy listo").whenLost("Haz doble clic en la Moneda en la lista de la escena.", "scene.objlist|tab.scene|menu.proyecto")
                .doIt([](Project &p) { std::string id = objIdNamed(p, "moneda"); if (json *o = findById(p.doc["objects"], id)) paintSimple(p, *o, 1); }),
            Step("Pon al menos 3 monedas en el nivel: arrastra la Moneda desde la lista a la escena, tres veces.", "En el aire, donde el personaje llegue saltando.")
                .ok([](const json &d) { return instancesNamed(d, "moneda") >= 3; }).at("scene.objlist").whenLost(kToScene, "tab.scene|scene.back|menu.proyecto")
                .doIt([](Project &p) { std::string id = objIdNamed(p, "moneda"); for (int k = 0; k < 3; k++) placeIn(p, id, 260.0f + k * 70, 260); }),
            Step("Una variable es una cajita con un número. Abre el panel ☰ y pulsa \"Ajustes y variables globales\".",
                 "Las variables globales se guardan aunque cambies de escena.")
                .ok([](const json &d) { return noted("settings") || hasGlobal(d, "puntos"); }).at("panel.ajustes|menu.proyecto")
                .doIt([](Project &p) { if (!hasGlobal(p.doc, "puntos")) { p.doc["globals"].push_back({{"name", "puntos"}, {"value", 0}}); p.dirty = true; } }),
            Step("En Variables globales, añade una que se llame puntos (empieza en 0).", "")
                .ok([](const json &d) { return hasGlobal(d, "puntos"); }).at("settings.globals")
                .whenLost("Abre el panel ☰ y pulsa \"Ajustes y variables globales\".", "panel.ajustes|menu.proyecto")
                .doIt([](Project &p) { if (!hasGlobal(p.doc, "puntos")) { p.doc["globals"].push_back({{"name", "puntos"}, {"value", 0}}); p.dirty = true; } }),
            Step("Ahora la regla. En Eventos, añade un evento con la condición Colisión (tu personaje con la Moneda) y las acciones Eliminar objeto (Moneda) y Sumar a la variable global puntos.",
                 "La condición elige la moneda que tocaste: solo esa se elimina.")
                .ok([](const json &d) { return hasEvent(d, {"collision"}, {"removeObj", "addGlobal"}); })
                .at("events.add|tab.events").whenLost("Abre la pestaña Eventos.", "tab.events|menu.proyecto")
                .doIt([](Project &p) {
                    json *pl = objBy(p, [](const json &o) { return platformer(o); });
                    std::string mon = objIdNamed(p, "moneda");
                    if (!pl || mon.empty()) return;
                    if (!hasGlobal(p.doc, "puntos")) p.doc["globals"].push_back({{"name", "puntos"}, {"value", 0}});
                    addEventTo(p, json::array({{{"type", "collision"}, {"params", {{"object", jstr(*pl, "id")}, {"with", mon}}}}}),
                               json::array({{{"type", "removeObj"}, {"params", {{"object", mon}}}}, {{"type", "addGlobal"}, {"params", {{"variable", "puntos"}, {"value", 1}}}}}));
                }),
            Step("Falta ver los puntos. Añade un objeto de tipo Texto, escribe en Texto: Puntos: {g:puntos} y ponlo en la escena.",
                 "{g:puntos} se cambia por el valor de la variable mientras juegas.")
                .ok([](const json &d) { for (auto &o : arrOf(d, "objects")) if (jstr(o, "type") == "text" && jstr(o.value("text", json::object()), "content").find("{g:") != std::string::npos && instancesOf(d, jstr(o, "id"))) return true; return false; })
                .at("newobj.text|scene.addobj|scene.objlist").whenLost(kToScene, "tab.scene|scene.back|menu.proyecto")
                .doIt([](Project &p) {
                    std::string id = createObject(p, "text");
                    if (json *o = findById(p.doc["objects"], id)) { (*o)["text"]["content"] = "Puntos: {g:puntos}"; if (json *sc = firstScene(p)) (*o)["scene"] = jstr(*sc, "id"); }
                    placeIn(p, id, 20, 20);
                }),
            Step("¡A probar! Vista previa y recoge las monedas: el marcador sube.", "").ok([](const json &) { return noted("preview"); }).at("toolbar.preview"),
            Step("¡Genial! Ya sabes usar variables. Con lo mismo puedes hacer vidas, tiempo o llaves.").next("¡Gracias, Floppa!")}, ensurePlatformer},

        {"musica", "Música con Source Sound", "Una canción ya hecha para ver cómo funciona, y que suene en tu juego.", "Música", {
            Step("¡Música! Te he preparado una canción de 32 segundos para que veas cómo está hecha.").next("¡A ver!"),
            Step("Abre el panel ☰, despliega \"Música y sonidos\" y pulsa \"+ Canción Test\".", "")
                .ok([](const json &d) { for (auto &m : arrOf(d, "music")) if (m.contains("song")) return true; return false; })
                .at("panel.cancion|panel.musica|menu.proyecto")
                .doIt([](Project &p) { if (onAddSong) onAddSong(); (void)p; }),
            Step("Se abrió en Source Sound. Pulsa el botón de play para escucharla.", "Espacio también la pone y la para.")
                .ok([](const json &) { return noted("sound.play"); }).at("sound.play")
                .whenLost("En el panel ☰ > Música y sonidos, pulsa Editar en la canción Test.", "panel.musica|menu.proyecto"),
            Step("Cada rectángulo morado es una nota: a la izquierda está el teclado (más arriba, más agudo) y hacia la derecha pasa el tiempo.",
                 "Clic: poner una nota. Arrastrar: moverla. Clic derecho: borrarla.").at("sound.roll").next()
                .whenLost("Vuelve a la pestaña de la canción.", "panel.musica|menu.proyecto"),
            Step("Estas son las pistas: melodía, piano, bajo, cuerdas y batería. Cada una tiene su instrumento. Elige otra para ver sus notas.",
                 "Cambia el instrumento de una pista para oír la diferencia.").at("sound.tracks").next()
                .whenLost("Vuelve a la pestaña de la canción.", "panel.musica|menu.proyecto"),
            Step("Que suene en el juego: en Eventos, añade un evento con la condición \"Al empezar la escena\" y la acción \"Reproducir audio\" con la canción.",
                 "Las dos están en el buscador: escribe empezar y reproducir.")
                .ok([](const json &d) { return hasEvent(d, {"atStart"}, {"playMusic"}); })
                .at("events.add|tab.events").whenLost("Abre la pestaña Eventos.", "tab.events|menu.proyecto")
                .doIt([](Project &p) {
                    std::string mid;
                    for (auto &m : p.doc["music"]) if (m.contains("song")) mid = jstr(m, "id");
                    if (mid.empty()) return;
                    addEventTo(p, json::array({{{"type", "atStart"}, {"params", json::object()}}}),
                               json::array({{{"type", "playMusic"}, {"params", {{"track", mid}, {"loop", "yes"}}}}}));
                }),
            Step("Pulsa Vista previa y escúchala en tu juego.", "").ok([](const json &) { return noted("preview"); }).at("toolbar.preview"),
            Step("¡Ya tienes música! Copia la canción, cambia notas y haz la tuya.").next("¡Gracias, Floppa!")}, nullptr},
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
static bool g_hint = false, g_min = false, g_needSetup = false;
static double g_farewellAt = -1;                // cerraste el proyecto: Floppa se despide y se va
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
        g_needSetup = true;   // lo que la leccion necesita en el proyecto (se prepara en drawPanel)
    }
}

/* ---------------- puntos que Floppa senala ---------------- */
struct Anchor { ImVec2 a, b; int frame; };
static std::map<std::string, Anchor> g_anchors;
void anchorRect(const char *id, ImVec2 a, ImVec2 b) { g_anchors[id] = {a, b, ImGui::GetFrameCount()}; }
void anchor(const char *id) { anchorRect(id, ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); }
// el primero de "a|b|c" que se haya dibujado en este fotograma o en el anterior
static const Anchor *findAnchor(const char *list) {
    if (!list) return nullptr;
    std::string l = list;
    int now = ImGui::GetFrameCount();
    for (size_t p = 0; p <= l.size();) {
        size_t q = l.find('|', p);
        std::string id = l.substr(p, q == std::string::npos ? std::string::npos : q - p);
        auto it = g_anchors.find(id);
        if (it != g_anchors.end() && now - it->second.frame <= 1 && it->second.b.x > it->second.a.x) return &it->second;
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return nullptr;
}

void drawSection() {
    load();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "Aprende haciendo: Floppa te dice qué hacer, te señala dónde pulsar y se da cuenta sola cuando lo haces.");
    ImGui::Dummy(ImVec2(0, 8));
    const auto &L = lessons();
    float w = std::min(560.0f, ImGui::GetContentRegionAvail().x);
    std::string group;
    for (int i = 0; i < (int)L.size(); i++) {
        const Lesson &l = L[i];
        if (group != l.group) {   // las notas van por temas
            group = l.group;
            ImGui::PushFont(ui::boldFont(), 19.0f);
            ImGui::TextUnformatted(l.group);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 2));
        }
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
        if (i + 1 < (int)L.size() && group != L[i + 1].group) ImGui::Dummy(ImVec2(0, 10));
    }
}

/* ---------------- Floppa ---------------- */
// Las dos caras (normal y contenta) van dentro del editor (src/editor/presets/floppa*.s2dsprite)
static ui::PixTexture *floppaFace(int k) {
    static ui::PixTexture tex[2];
    static bool loaded[2] = {false, false};
    if (!loaded[k]) {
        loaded[k] = true;
        const unsigned char *data = k ? preset_floppa2 : preset_floppa1;
        size_t len = k ? preset_floppa2_len : preset_floppa1_len;
        json j = json::parse(std::string((const char *)data, len), nullptr, false);
        int w = (int)jnum(j, "width", 32), h = (int)jnum(j, "height", 32);
        std::vector<Px> px(w * h);
        if (j.contains("frames") && j["frames"].is_array() && !j["frames"].empty() && j["frames"][0].contains("pixels"))
            for (int i = 0; i < w * h && i < (int)j["frames"][0]["pixels"].size(); i++) {
                const json &v = j["frames"][0]["pixels"][i];
                if (v.is_string()) px[i] = parseColor(v.get<std::string>());
            }
        tex[k].update(px, w, h);
    }
    return tex[k].tex.id ? &tex[k] : nullptr;
}
// cuantos bytes ocupan los primeros n caracteres (UTF-8)
static size_t utf8Prefix(const char *s, size_t n) {
    size_t i = 0;
    while (s[i] && n) { unsigned char c = (unsigned char)s[i]; i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; n--; }
    return i;
}

static double g_stepAt = 0, g_startAt = 0, g_nopeAt = -10, g_lostFor = 0;
static float g_fx = -1;                          // posicion x de Floppa (se desliza hacia su sitio)
static bool g_left = false;                      // Floppa a la izquierda (si lo que senala esta abajo a la derecha)
static int g_lastStep = -1, g_wrong = 0;         // clics fuera de lo que senala en este paso
static float g_arrowA = 1;                       // la flecha se aparta mientras dibujas o arrastras

static bool g_pressReady = false;
void pressReady() { g_pressReady = true; }
static bool g_reached = false;                   // ya pulsaste donde senalaba: la flecha no vuelve en este paso
static bool g_pressAuto = false;                 // pruebas: "Hazlo por mi"
void projectClosed() { if (g_lesson >= 0 && g_farewellAt < 0) g_farewellAt = ImGui::GetTime(); }

// Floppa y su globo (comun a la leccion y a la despedida); body dibuja el contenido del globo
static void floppaFrame(double now, bool happy, bool typing, ImVec2 &bubA, ImVec2 &bubB, const std::function<void()> &body, bool leaving) {
    ImGuiViewport *vp = ImGui::GetMainViewport();
    const float F = 96, W = 380, M = 18;
    float wantX = leaving ? vp->WorkPos.x + vp->WorkSize.x + 60 : g_left ? vp->WorkPos.x + M : vp->WorkPos.x + vp->WorkSize.x - M - F;
    if (g_fx < 0) { g_fx = vp->WorkPos.x + vp->WorkSize.x + 40; g_startAt = now; }   // entra desde el borde
    g_fx += (wantX - g_fx) * std::min(1.0f, ImGui::GetIO().DeltaTime * (leaving ? 3.0f : 6.0f));
    float bob = typing ? (float)std::sin(now * 14) * 2 : happy ? -(float)std::fabs(std::sin(now * 8)) * 8 : (float)std::sin(now * 2) * 2;
    float fy = vp->WorkPos.y + vp->WorkSize.y - M - F + bob;
    bool blink = !happy && std::fmod(now, 3.7) < 0.14;
    float bx = g_left ? g_fx + F + 14 : g_fx - 14 - W;
    ImGui::SetNextWindowPos(ImVec2(bx, vp->WorkPos.y + vp->WorkSize.y - M), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(W, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::kPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, ui::kAccent);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    if (ImGui::Begin("##escuela", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        body();
        bubA = ImGui::GetWindowPos();
        bubB = ImVec2(bubA.x + ImGui::GetWindowSize().x, bubA.y + ImGui::GetWindowSize().y);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    float tailY = std::min(bubB.y - 22, fy + F * 0.45f);
    if (g_left) fg->AddTriangleFilled(ImVec2(bubA.x + 1, tailY - 9), ImVec2(bubA.x + 1, tailY + 9), ImVec2(g_fx + F + 2, fy + F * 0.5f), ui::kPanel);
    else fg->AddTriangleFilled(ImVec2(bubB.x - 1, tailY - 9), ImVec2(bubB.x - 1, tailY + 9), ImVec2(g_fx - 2, fy + F * 0.5f), ui::kPanel);
    fg->AddCircleFilled(ImVec2(g_fx + F / 2, vp->WorkPos.y + vp->WorkSize.y - M + 2), F * 0.36f, IM_COL32(0, 0, 0, 70), 24);   // sombra
    if (ui::PixTexture *t = floppaFace(happy || blink ? 1 : 0)) {
        if (g_left) fg->AddImage(t->id(), ImVec2(g_fx + F, fy), ImVec2(g_fx, fy + F), ImVec2(0, 0), ImVec2(1, 1));   // mira hacia el globo
        else fg->AddImage(t->id(), ImVec2(g_fx, fy), ImVec2(g_fx + F, fy + F));
    }
}

// recuadro que late y flecha curva desde Floppa hasta lo que hay que pulsar
static void pointAt(const Anchor *tg, double now, bool arrow, const char *label) {
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    const float F = 96, M = 18;
    float fy = vp->WorkPos.y + vp->WorkSize.y - M - F;
    float pulse = 0.5f + 0.5f * (float)std::sin(now * 5);
    ImVec2 a(tg->a.x - 5, tg->a.y - 5), b(tg->b.x + 5, tg->b.y + 5);
    fg->AddRect(a, b, IM_COL32(157, 125, 255, (int)(140 + 115 * pulse)), 8.0f, 0, 3.0f);
    fg->AddRect(ImVec2(a.x - 4 * pulse, a.y - 4 * pulse), ImVec2(b.x + 4 * pulse, b.y + 4 * pulse), IM_COL32(157, 125, 255, (int)(90 * (1 - pulse))), 10.0f, 0, 2.0f);
    // mientras dibujas o arrastras, la flecha se aparta (no tapa lo que haces)
    g_arrowA += ((ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) ? 0.08f : 1.0f) - g_arrowA) * std::min(1.0f, ImGui::GetIO().DeltaTime * 10);
    if (!arrow || g_arrowA < 0.05f) return;
    int al = (int)(230 * g_arrowA);
    ImVec2 from(g_fx + F / 2, fy + 6);
    ImVec2 c((a.x + b.x) / 2, (a.y + b.y) / 2);
    ImVec2 to(std::clamp(from.x, a.x, b.x), std::clamp(from.y, a.y, b.y));
    if (to.x == from.x && to.y == from.y) to = c;
    float dx = to.x - from.x, dy = to.y - from.y, len = std::sqrt(dx * dx + dy * dy);
    if (len > 40) {
        ImVec2 c1(from.x, from.y - std::min(160.0f, len * 0.4f)), c2(to.x - dx * 0.15f, to.y - dy * 0.35f);
        ImU32 col = IM_COL32(157, 125, 255, al);
        fg->AddBezierCubic(from, c1, c2, to, col, 3.0f, 32);
        float tx = to.x - c2.x, ty = to.y - c2.y, tl = std::sqrt(tx * tx + ty * ty);
        if (tl > 0.01f) {
            tx /= tl; ty /= tl;
            fg->AddTriangleFilled(to, ImVec2(to.x - tx * 14 - ty * 8, to.y - ty * 14 + tx * 8), ImVec2(to.x - tx * 14 + ty * 8, to.y - ty * 14 - tx * 8), col);
        }
    }
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 lp(std::clamp(c.x - ts.x / 2 - 6, vp->WorkPos.x + 4, vp->WorkPos.x + vp->WorkSize.x - ts.x - 16), a.y - ts.y - 12);
    if (lp.y < vp->WorkPos.y + 2) lp.y = b.y + 6;
    fg->AddRectFilled(lp, ImVec2(lp.x + ts.x + 12, lp.y + ts.y + 6), (ui::kAccent & 0x00FFFFFF) | ((ImU32)(255 * g_arrowA) << 24), 6);
    fg->AddText(ImVec2(lp.x + 6, lp.y + 3), IM_COL32(255, 255, 255, (int)(255 * g_arrowA)), label);
}

void drawPanel(Project *proj) {
    if (g_lesson < 0) return;
    double now = ImGui::GetTime();
    ImVec2 bubA, bubB;
    if (!proj && g_farewellAt < 0) g_farewellAt = now;
    if (g_farewellAt >= 0) {   // cerraste el proyecto: se despide y se va
        double t = now - g_farewellAt;
        floppaFrame(now, false, false, bubA, bubB, [] {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kAccent), "Floppa");
            ImGui::PushTextWrapPos(380 - 16);
            ImGui::PushFont(nullptr, ImGui::GetFontSize() + 1.5f);
            ImGui::TextUnformatted("¿No te dieron ganas? No pasa nada, ¡otro día será! Te espero en la Escuela.");
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
        }, t > 2.6);
        if (t > 4.0) { g_lesson = -1; g_fx = -1; g_farewellAt = -1; }
        return;
    }
    const Lesson &l = lessons()[g_lesson];
    if (g_needSetup) { g_needSetup = false; if (l.setup) l.setup(*proj); }
    int n = (int)l.steps.size();
    if (g_step >= n) g_step = n - 1;
    const Step &s = l.steps[g_step];
    static int lastLesson = -1;
    if (lastLesson != g_lesson) { lastLesson = g_lesson; g_fx = -1; g_lastStep = -1; g_left = false; }   // otra leccion: Floppa vuelve a entrar
    if (g_lastStep != g_step) { g_lastStep = g_step; g_stepAt = now; g_nopeAt = -10; g_reached = false; g_wrong = 0; g_lostFor = 0; }
    auto complete = [&]() {
        g_doneAt = now;
        g_done[l.id] = std::max(g_done[l.id], g_step + 1);
        save();
    };
    if (g_doneAt < 0 && !s.ready && s.check && s.check(proj->doc)) complete();   // paso cumplido
    if (g_doneAt >= 0 && now - g_doneAt > 1.2) {                    // tras un momento, el siguiente
        g_doneAt = -1; g_hint = false;
        if (g_step + 1 < n) g_step++;
        else { g_lesson = -1; g_fx = -1; if (onNotify) onNotify("Lección terminada: " + std::string(l.title)); return; }
    }
    ImGuiViewport *vp = ImGui::GetMainViewport();
    const Anchor *tg = g_doneAt < 0 ? findAnchor(s.target) : nullptr;
    // te perdiste: lo que tiene que senalar no esta a la vista (otra pestana, un panel cerrado...)
    if (!tg && s.target && !g_reached && g_doneAt < 0) g_lostFor += ImGui::GetIO().DeltaTime; else g_lostFor = 0;
    bool lost = g_lostFor > 1.2;
    const Anchor *lostTg = lost ? findAnchor(s.lostTarget ? s.lostTarget : "tab.scene|menu.proyecto") : nullptr;
    static ImVec2 lastBubA, lastBubB;            // globo del fotograma anterior (los clics en el no cuentan)
    if (tg && !g_reached && ImGui::IsMouseClicked(0)) {
        ImVec2 m = ImGui::GetIO().MousePos;
        bool onBubble = m.x >= lastBubA.x && m.x < lastBubB.x && m.y >= lastBubA.y && m.y < lastBubB.y;
        bool onTarget = m.x >= tg->a.x - 5 && m.x < tg->b.x + 5 && m.y >= tg->a.y - 5 && m.y < tg->b.y + 5;
        if (onTarget) g_reached = true;
        else if (!onBubble) g_wrong++;           // pulsaste en otro sitio: la flecha sigue ahi
    }
    // lado: si lo que senala queda abajo a la derecha, Floppa se va a la izquierda (cruza la pantalla deslizandose)
    if (const Anchor *pt = tg ? tg : lostTg) {
        float cx = (pt->a.x + pt->b.x) / 2, cy = (pt->a.y + pt->b.y) / 2;
        bool under = cy > vp->WorkPos.y + vp->WorkSize.y * 0.55f;
        if (under && cx > vp->WorkPos.x + vp->WorkSize.x * 0.55f) g_left = true;
        else if (under && cx < vp->WorkPos.x + vp->WorkSize.x * 0.45f) g_left = false;
    }
    bool happy = g_doneAt >= 0 || (s.ready && !s.check && g_step + 1 == n);
    size_t total = strlen(s.text), shown = std::min(total, utf8Prefix(s.text, (size_t)std::max(0.0, (now - g_stepAt) * 45)));
    bool typing = shown < total;
    // "Hazlo por mi": si te equivocaste, si Floppa dijo "todavia no" o si llevas un rato en el paso
    bool offerAuto = s.autoDo && g_doneAt < 0 && (g_wrong >= 2 || now - g_nopeAt < 30 || now - g_stepAt > 25 || lost);
    Project &P = *proj;
    floppaFrame(now, happy, typing, bubA, bubB, [&] {
        float W = 380;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kAccent), "Floppa");
        ImGui::SameLine(); ImGui::TextDisabled("· %s · paso %d de %d", l.title, g_step + 1, n);
        ImGui::SameLine(W - 82);
        if (ImGui::SmallButton(g_min ? "Abrir" : "Ocultar")) g_min = !g_min;
        if (g_min) return;
        ImVec2 bp = ImGui::GetCursorScreenPos();
        float frac = (float)(g_step + (g_doneAt >= 0 ? 1 : 0)) / (float)n;
        dl->AddRectFilled(bp, ImVec2(bp.x + W - 32, bp.y + 4), ui::kLine, 2);
        dl->AddRectFilled(bp, ImVec2(bp.x + (W - 32) * ui::animTo(ImGui::GetID("##esc_bar"), frac, 8.0f), bp.y + 4), ui::kAccent, 2);
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushTextWrapPos(W - 16);
        if (g_doneAt >= 0) ImGui::TextColored(ImVec4(0.45f, 0.86f, 0.55f, 1), "¡Bien hecho!");
        ImGui::PushFont(nullptr, ImGui::GetFontSize() + 1.5f);
        ImGui::TextUnformatted(s.text, s.text + shown);
        ImGui::PopFont();
        if (ImGui::IsItemClicked()) g_stepAt = -1e9;   // clic: el texto sale entero
        if (lost) ImGui::TextColored(ImVec4(1, 0.78f, 0.45f, 1), "¿Te perdiste? %s", s.lost ? s.lost : kToScene);
        else if (g_wrong >= 2 && tg && !g_reached) ImGui::TextColored(ImVec4(1, 0.78f, 0.45f, 1), "¡Casi! Es donde brilla el recuadro morado.");
        if (now - g_nopeAt < 3) ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "¡Todavía no! %s", s.hint && *s.hint ? s.hint : "Hazlo y vuelve a pulsar.");
        if (g_hint && s.hint && *s.hint) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kText2), "%s", s.hint);
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 4));
        if (s.ready && g_doneAt < 0) {
            bool press = g_pressReady;
            g_pressReady = false;
            if (ui::primaryButton(s.readyLabel ? s.readyLabel : "Estoy listo", ImVec2(150, 0)) || press) {
                if (!s.check || s.check(P.doc)) complete(); else g_nopeAt = now;
            }
            ImGui::SameLine();
        }
        bool autoPress = g_pressAuto && s.autoDo && g_doneAt < 0;
        g_pressAuto = false;
        if (autoPress) { s.autoDo(P); if (!s.check || s.check(P.doc)) complete(); }
        if (offerAuto) {
            if (ui::primaryButton("Hazlo por mí", ImVec2(0, 0))) { s.autoDo(P); if (!s.check || s.check(P.doc)) complete(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Floppa hace este paso por ti y seguís con el siguiente");
            ImGui::SameLine();
        }
        if (s.hint && *s.hint && ImGui::SmallButton(g_hint ? "Ocultar pista" : "Pista")) g_hint = !g_hint;
        ImGui::SameLine();
        if (g_step > 0 && ImGui::SmallButton("Anterior")) { g_step--; g_doneAt = -1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Saltar")) { g_doneAt = -1; if (g_step + 1 < n) g_step++; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Salir")) { g_lesson = -1; g_fx = -1; }
    }, false);
    lastBubA = bubA; lastBubB = bubB;
    if (g_lesson < 0 || g_min) return;
    if (tg) pointAt(tg, now, !g_reached, "¡Aquí!");
    else if (lostTg) pointAt(lostTg, now, true, "Por aquí");
}
// pruebas: "Hazlo por mi" del paso actual
bool autoStep(Project &) {   // como pulsar el boton (en el siguiente fotograma)
    if (g_lesson < 0 || !lessons()[g_lesson].steps[g_step].autoDo) return false;
    g_pressAuto = true;
    return true;
}
}
