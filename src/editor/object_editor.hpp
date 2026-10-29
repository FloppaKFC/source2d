// Source 2D - editor de objetos nativo: propiedades, animaciones, comportamientos, variables y efecto.
#pragma once
#include <functional>
#include <string>
#include "assets.hpp"

std::string createObject(Project &proj, const std::string &kind);      // devuelve el id
std::vector<std::string> objBehaviorTypes(const json &obj);            // comportamientos (antiguos y de la lista)
void objectAddBehavior(json &obj, const std::string &type);
void objectRemoveBehavior(json &obj, const std::string &type);
// frames de un sprite (imagen, capas y duraciones a la vez): 'm' mover i a j, 'd' borrar i, 'c' duplicar i, 'n' uno vacio al final
void objectFrameOp(json &sp, char op, int i, int j);

class ObjectEditor {
public:
    std::string objectId;
    bool open = true, focusMe = true;
    std::function<void(const std::string &)> onOpenSprite;            // abrir un sprite en su editor
    std::function<std::string(const std::string &, int, int)> onNewSprite;
    std::function<void(const std::string &)> onOpenTileset, onImportTiles;
    std::function<void(const std::string &)> onSpriteChanged;          // se cambiaron sus frames desde aqui   // onImportTiles: id del objeto Tile
    int forceTab = -1;                                                 // pruebas: seleccionar una pestana

    void draw(Project &proj);
    void drawBody(Project &proj);                                      // sin ventana (dentro de la escena)
    std::string title(Project &proj);
    int curAnim = 0;                                                   // animacion elegida (tambien la de la colision)
    int colFrame = 1;                                                  // frame de fondo en el editor de colisiones
    std::function<void(const std::string &)> onStatus;

private:
    int tab = 0, nextTab = -1;
    JsonHistory hist;
    bool showCatalog = false;
    void tabProps(Project &proj, json &o);
    void tabAnims(Project &proj, json &o);
    void framesStrip(Project &proj, json &anim);
    void addAnimation(Project &proj, json &o);
    void tabBehaviors(Project &proj, json &o);
    void tabVars(json &o);
    void tabEffect(Project &proj, json &o);
};
