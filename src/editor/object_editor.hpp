// Source 2D - editor de objetos nativo: propiedades, animaciones, comportamientos, variables y efecto.
#pragma once
#include <functional>
#include <string>
#include "assets.hpp"

std::string createObject(Project &proj, const std::string &kind);      // devuelve el id
std::vector<std::string> objBehaviorTypes(const json &obj);            // comportamientos (antiguos y de la lista)
void objectAddBehavior(json &obj, const std::string &type);
void objectRemoveBehavior(json &obj, const std::string &type);

class ObjectEditor {
public:
    std::string objectId;
    bool open = true, focusMe = true;
    std::function<void(const std::string &)> onOpenSprite;            // abrir un sprite en su editor
    std::function<std::string(const std::string &, int, int)> onNewSprite;
    int forceTab = -1;                                                 // pruebas: seleccionar una pestana

    void draw(Project &proj);
    std::string title(Project &proj);

private:
    int tab = 0;
    JsonHistory hist;
    bool showCatalog = false;
    void tabProps(Project &proj, json &o);
    void tabAnims(Project &proj, json &o);
    void tabBehaviors(Project &proj, json &o);
    void tabVars(json &o);
    void tabEffect(Project &proj, json &o);
};
