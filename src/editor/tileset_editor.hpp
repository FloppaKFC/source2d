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
    void reload(Project &proj);
    void writeBack(Project &proj);
    void setGrid(Project &proj, int tileSize, int cols, int rows);
private:
    bool loaded = false;
};
