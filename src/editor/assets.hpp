#pragma once
#include <map>
#include <string>
#include <vector>
#include "project.hpp"
#include "ui.hpp"

double jnum(const json &o, const char *key, double def);
std::string jstr(const json &o, const char *key, const std::string &def = "");
bool jbool(const json &o, const char *key, bool def);
json *findById(json &arr, const std::string &id);

bool objectInScene(const json &obj, const std::string &sceneId);
json sceneObjects(Project &proj, const std::string &sceneId);
json &sceneEvents(Project &proj, const std::string &sceneId);
std::string sceneName(Project &proj, const std::string &sceneId);

ui::PixTexture *spriteFrameTex(Project &proj, const std::string &spriteId, int frame);
ui::PixTexture *tileTex(Project &proj, const std::string &tilesetId, int tile);
void releaseTextures();

const char *objTypeLabel(const std::string &type);

struct ObjSize { float w, h; };
ObjSize objBaseSize(Project &proj, const json &obj);
void drawObjectAt(ImDrawList *dl, Project &proj, const json &obj, ImVec2 center, float scaleX, float scaleY,
                  float angleDeg, float opacity, float zoom, int frame);
void objectThumb(Project &proj, const json &obj, float size);
void objectThumbAt(ImDrawList *dl, Project &proj, const json &obj, ImVec2 p, float size);

bool editNum(const char *label, json &o, const char *key, double def, float speed = 1.0f, const char *fmt = "%.0f", double mn = 0, double mx = 0);
bool editInt(const char *label, json &o, const char *key, int def, int mn = 0, int mx = 0);
bool editText(const char *label, json &o, const char *key, const char *hint = nullptr);
bool editMultiline(const char *label, json &o, const char *key, float height);
bool editBool(const char *label, json &o, const char *key, bool def);
bool editColor(const char *label, json &o, const char *key, const char *def);
bool comboById(const char *label, json &o, const char *key, json &list, const char *noneLabel = nullptr);
bool comboOptions(const char *label, json &o, const char *key, const std::vector<std::pair<std::string, std::string>> &opts, const std::string &def);

struct JsonHistory {
    std::vector<json> undo, redo;
    json stable;
    bool init = false;
    void track(const json &cur);
    bool doUndo(json &cur);
    bool doRedo(json &cur);
};
