#pragma once
#include <functional>
#include <string>
#include "assets.hpp"

std::string createObject(Project &proj, const std::string &kind);
std::vector<std::string> objBehaviorTypes(const json &obj);
void objectAddBehavior(json &obj, const std::string &type);
void objectRemoveBehavior(json &obj, const std::string &type);
void objectFrameOp(json &sp, char op, int i, int j);

class ObjectEditor {
public:
    std::string objectId;
    bool open = true, focusMe = true;
    std::function<void(const std::string &)> onOpenSprite;
    std::function<std::string(const std::string &, int, int)> onNewSprite;
    std::function<void(const std::string &)> onOpenTileset, onImportTiles;
    std::function<void(const std::string &)> onSpriteChanged;
    int forceTab = -1;

    void draw(Project &proj);
    void drawBody(Project &proj);
    std::string title(Project &proj);
    int curAnim = 0;
    int colFrame = 1;
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
