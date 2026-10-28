// Source 2D - fuentes del proyecto: TTF/OTF importadas o de pixeles dibujadas en el editor (un glifo por
// caracter, como los tiles). Tambien dibuja textos con esas fuentes para las vistas del editor.
#pragma once
#include <string>
#include <vector>
#include "assets.hpp"
#include "sprite_editor.hpp"

extern const char *kDefaultFontChars;   // caracteres de una fuente de pixeles nueva

// crea una fuente de pixeles; con fromGameFont los glifos empiezan con la fuente del juego pasada a pixeles
std::string createPixelFont(Project &proj, const std::string &name, int cellW, int cellH, bool fromGameFont);
std::string importFont(Project &proj, const std::string &file, std::string &err);   // TTF/OTF; devuelve el id
void fillGlyphsFromGameFont(json &font);                                            // rehace los glifos a su tamano

// ancho de un texto en pixeles de la fuente (sin escalar) y alto de la celda
float pixelTextWidth(const json &font, const std::string &text);

// imagen de un texto: px es de w x h y se muestra a w*scale x h*scale (las fuentes de pixeles se guardan a 1:1)
struct TextImage { std::vector<Px> px; int w = 0, h = 0; float scale = 1; };
bool renderTextImage(const json &doc, const std::string &fontId, const std::string &text, float size, Px color, bool bold, TextImage &out);
// lo mismo como textura cacheada (se libera sola si no se usa); nullptr si el texto esta vacio
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
