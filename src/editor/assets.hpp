// Source 2D - recursos compartidos por los editores: texturas de sprites y tiles, dibujo de instancias,
// campos ligados al JSON del proyecto e historial de deshacer.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "project.hpp"
#include "ui.hpp"

// numero de un campo JSON (acepta enteros, reales y texto numerico)
double jnum(const json &o, const char *key, double def);
std::string jstr(const json &o, const char *key, const std::string &def = "");
bool jbool(const json &o, const char *key, bool def);
json *findById(json &arr, const std::string &id);

// objetos y eventos por escena (como en GDevelop): un objeto con "scene" pertenece a esa escena; sin "scene" es
// global y sale en todas. Cada escena tiene sus eventos (scene["events"]); doc["events"] son los eventos comunes,
// que se ejecutan en todas las escenas (sus funciones se pueden llamar desde cualquiera).
bool objectInScene(const json &obj, const std::string &sceneId);              // sceneId "" = todas
json sceneObjects(Project &proj, const std::string &sceneId);                 // copia: los de la escena y los globales
json &sceneEvents(Project &proj, const std::string &sceneId);                 // "" = eventos comunes
std::string sceneName(Project &proj, const std::string &sceneId);

// texturas: se invalidan cuando cambia proj.rev
ui::PixTexture *spriteFrameTex(Project &proj, const std::string &spriteId, int frame);
ui::PixTexture *tileTex(Project &proj, const std::string &tilesetId, int tile);
void releaseTextures();

// nombre del tipo de objeto para la interfaz ("Sprite", "Texto"...)
const char *objTypeLabel(const std::string &type);

// tamano base del objeto (sin escala) y dibujo de una instancia como la dibuja el juego
struct ObjSize { float w, h; };
ObjSize objBaseSize(Project &proj, const json &obj);
void drawObjectAt(ImDrawList *dl, Project &proj, const json &obj, ImVec2 center, float scaleX, float scaleY,
                  float angleDeg, float opacity, float zoom, int frame);
void objectThumb(Project &proj, const json &obj, float size);   // miniatura en la posicion actual (avanza el cursor)
void objectThumbAt(ImDrawList *dl, Project &proj, const json &obj, ImVec2 p, float size);   // sin mover el cursor

// campos de edicion ligados a una clave del JSON; devuelven true si cambiaron
bool editNum(const char *label, json &o, const char *key, double def, float speed = 1.0f, const char *fmt = "%.0f", double mn = 0, double mx = 0);
bool editInt(const char *label, json &o, const char *key, int def, int mn = 0, int mx = 0);
bool editText(const char *label, json &o, const char *key, const char *hint = nullptr);
bool editMultiline(const char *label, json &o, const char *key, float height);
bool editBool(const char *label, json &o, const char *key, bool def);
bool editColor(const char *label, json &o, const char *key, const char *def);
bool comboById(const char *label, json &o, const char *key, json &list, const char *noneLabel = nullptr);
bool comboOptions(const char *label, json &o, const char *key, const std::vector<std::pair<std::string, std::string>> &opts, const std::string &def);

// historial por documento: guarda un paso cuando termina la interaccion (sin raton ni campo activo)
struct JsonHistory {
    std::vector<json> undo, redo;
    json stable;
    bool init = false;
    void track(const json &cur);              // llamar cada fotograma despues de dibujar
    bool doUndo(json &cur);
    bool doRedo(json &cur);
};
