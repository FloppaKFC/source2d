// Source 2D - objetos Tile: cada instancia de un objeto de tipo "tile" lleva su propio mapa (inst["tm"]: cols, rows,
// data) con la esquina de arriba a la izquierda en (x, y). Al pintar fuera se agranda y al borrar en el borde se
// achica solo. Los autotiles (doc["autotiles"]) eligen el tile de cada celda segun sus vecinos.
#pragma once
#include "assets.hpp"

struct TileRect { float x, y, w, h; };

bool isTileObj(const json *obj);
float tileSizeOf(Project &proj, const json &obj);                              // lado del tile del conjunto del objeto
bool tileInstRect(Project &proj, const json &inst, const json &obj, TileRect &r);   // false si no tiene mapa
// pinta (tile >= 0) o borra (tile < 0) la celda bajo (wx, wy); devuelve false si la instancia queda vacia
bool tileInstPaint(Project &proj, json &inst, const json &obj, float wx, float wy, int tile);
// origin: el punto (0, 0) del mundo en la pantalla; viewA/viewB: la parte visible en coordenadas del mundo
void drawTileInst(ImDrawList *dl, Project &proj, const json &inst, const json &obj, ImVec2 origin, float zoom, ImVec2 viewA, ImVec2 viewB, ImU32 tint);

// autotiles: {id, name, tilesetId, tiles: [16]} (mascara de vecinos iguales: arriba 1, derecha 2, abajo 4, izquierda 8)
json &projectAutotiles(Project &proj);
json *autotileOf(Project &proj, const json &obj);                              // el autotile activo del objeto o null
int autotilePick(const json &at, int mask);
bool autotileHas(const json &at, int tile);

void starterTiles(json &ts);   // tiles 0 y 1 de un conjunto nuevo: cesped y tierra, solidos

// proyectos de antes: los mapas de tiles de la escena pasan a ser instancias de objetos Tile
void migrateSceneTilemaps(json &doc);
