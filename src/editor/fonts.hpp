#pragma once
#include <string>
#include <vector>
#include "assets.hpp"
#include "sprite_editor.hpp"

extern const char *kDefaultFontChars;

std::string createPixelFont(Project &proj, const std::string &name, int cellW, int cellH, bool fromGameFont);
std::string importFont(Project &proj, const std::string &file, std::string &err);
void fillGlyphsFromGameFont(json &font);

float pixelTextWidth(const json &font, const std::string &text);

struct TextImage { std::vector<Px> px; int w = 0, h = 0; float scale = 1; };
bool renderTextImage(const json &doc, const std::string &fontId, const std::string &text, float size, Px color, bool bold, TextImage &out);
ui::PixTexture *textTexture(Project &proj, const std::string &fontId, const std::string &text, float size, const std::string &color, bool bold, float *scale);
void fontsNewFrame();
void releaseFontCache();

class FontEditor {
public:
    std::string fontId;
    bool open = true, focusMe = true;
    SpriteEditor pix;
    char sample[256] = "Hola, mundo! 0123";
    void draw(Project &proj);
    std::string title(Project &proj);
    void reload(Project &proj);
    void writeBack(Project &proj);
    void setCell(Project &proj, int w, int h);
    void setChars(Project &proj, const std::string &chars);
private:
    bool loaded = false;
};
