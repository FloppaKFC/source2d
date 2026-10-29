#pragma once
#include "assets.hpp"

struct TileRect { float x, y, w, h; };

bool isTileObj(const json *obj);
float tileSizeOf(Project &proj, const json &obj);
bool tileInstRect(Project &proj, const json &inst, const json &obj, TileRect &r);
bool tileInstPaint(Project &proj, json &inst, const json &obj, float wx, float wy, int tile);
void drawTileInst(ImDrawList *dl, Project &proj, const json &inst, const json &obj, ImVec2 origin, float zoom, ImVec2 viewA, ImVec2 viewB, ImU32 tint);

json &projectAutotiles(Project &proj);
json *autotileOf(Project &proj, const json &obj);
int autotilePick(const json &at, int mask);
bool autotileHas(const json &at, int tile);

void starterTiles(json &ts);

void migrateSceneTilemaps(json &doc);
