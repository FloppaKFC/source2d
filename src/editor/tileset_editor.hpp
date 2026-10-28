// Source 2D - editor de conjuntos de tiles: rejilla de tiles, marcas (solido, traspasable, escalera) y dibujo
// con el editor de sprites en modo tiles (cada tile es un frame).
#pragma once
#include <string>
#include "assets.hpp"
#include "sprite_editor.hpp"

std::string createTileset(Project &proj, const std::string &name, int tileSize, int cols, int rows);

class TilesetEditor {
public:
    std::string tilesetId;
    bool open = true, focusMe = true;
    SpriteEditor pix;
    void draw(Project &proj);
    std::string title(Project &proj);
    void reload(Project &proj);              // vuelve a cargar los tiles desde el proyecto
    void writeBack(Project &proj);           // guarda los pixeles en el proyecto
    void setGrid(Project &proj, int tileSize, int cols, int rows);
private:
    bool loaded = false;
};
