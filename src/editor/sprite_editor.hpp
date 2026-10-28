// Source 2D - editor de sprites nativo (interfaz y herramientas inspiradas en LibreSprite, codigo propio).
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "project.hpp"
#include "ui.hpp"

struct SpriteLayer {
    std::string name = "Capa 1";
    bool visible = true, locked = false;
    int opacity = 255;
    std::vector<std::vector<Px>> cels;       // un lienzo w*h por frame
};

struct SpriteState {                          // unidad de deshacer
    int w = 16, h = 16;
    std::vector<SpriteLayer> layers;
    std::vector<int> durations;               // ms por frame
};

enum class Tool { Pencil, Eraser, Bucket, Picker, Line, Rect, Ellipse, Select, Move, Hand, Lasso, Wand, Spray, Gradient, Contour, SelEllipse };

// opciones compartidas por todos los editores de sprites (como en LibreSprite)
struct SpriteToolState {
    Tool tool = Tool::Pencil;
    int brush = 1;
    bool pixelPerfect = false, fillShapes = false, contiguous = true;
    int wandTolerance = 0;                    // varita magica: diferencia de color admitida (0-255)
    bool wandContiguous = true;
    int sprayDensity = 6;                     // aerografo: puntos por paso
    bool gradDither = false;                  // degradado con tramado (pixel art)
    bool mirrorX = false, mirrorY = false, onion = false, grid = true;
    Px fg{20, 12, 28, 255}, bg{0, 0, 0, 0};
    std::vector<Px> palette;
    std::vector<Px> clip; int clipW = 0, clipH = 0;
};
SpriteToolState &spriteTools();
void loadPaletteFrom(const json &doc);
void savePaletteTo(json &doc);
// paletas en archivos, compatibles con Aseprite: .gpl (GIMP), .pal (JASC y RIFF), .hex (Lospec), .txt (Paint.NET),
// .act (Adobe), .ase/.aseprite (la paleta del archivo) e imagenes (.png, .gif, .bmp...: sus colores)
bool loadPaletteFile(const std::string &path, std::vector<Px> &out, std::string &err);
bool savePaletteFile(const std::string &path, const std::vector<Px> &pal, std::string &err);   // .gpl .pal .hex .txt .act .png
extern const char *kPaletteOpenExt, *kPaletteSaveExt;

struct SelRect { int x = 0, y = 0, w = 0, h = 0; bool active = false; };

class SpriteEditor {
public:
    std::string spriteId, name;
    bool open = true, focusMe = true;
    SpriteState st;
    int curLayer = 0, curFrame = 0;
    std::string status;

    bool load(const json &sprite);
    void writeTo(json &sprite) const;
    void reload(const json &sprite);           // cambios de otra persona: sin mover fotograma ni capa
    void draw(Project &proj);                  // ventana completa
    void drawBody(Project &proj);              // contenido, dentro de la ventana actual
    bool tileMode = false;                     // editor de tiles: cada frame es un tile, sin linea de tiempo
    ~SpriteEditor();

    // operaciones (tambien usadas por las pruebas automaticas)
    void pushUndo();
    void undo();
    void redo();
    void strokeBegin(int x, int y, bool secondary);
    void strokeMove(int x, int y);
    void strokeEnd();
    void addFrame(bool duplicate);
    void deleteFrame();
    void addLayer();
    void deleteLayer();
    void moveLayer(int dir);
    void resizeCanvas(int nw, int nh, bool center);
    void flip(bool horizontal);
    void rotate90();
    void rotate180();
    void rotate90ccw();
    void scaleSprite(int percent);             // vecino mas cercano, todos los frames y capas
    void duplicateLayer();
    void mergeDown();                          // la capa actual se pega sobre la de abajo
    void moveFrame(int dir);
    // efectos de imagen sobre el frame actual (o todos): 0 contorno, 1 invertir, 2 escala de grises, 3 brillo y contraste,
    // 4 tono y saturacion, 5 reemplazar color, 6 desplazar (con vuelta), 7 quitar pixeles sueltos
    void applyFx(int kind, bool allFrames, float a, float b, Px c1, Px c2);
    void selectAll();
    void clearSelection();
    void copySel(bool cut);
    void paste();
    void composite(int frame, std::vector<Px> &out, bool withPreview) const;
    // seleccion y corte
    void selectRect(int x, int y, int w, int h, int mode = 0);   // mode: 0 reemplaza, 1 suma, 2 resta
    void selectByColor(int x, int y, bool contiguous, int mode = 0);
    void invertSelection();
    void cropToSelection();                    // el lienzo pasa a ser el rectangulo de la seleccion (todas las capas y frames)
    void trimCanvas();                         // quita los bordes transparentes de todos los frames
    void cutToNewLayer();                      // lo seleccionado sale de su capa y va a una capa nueva
    void sliceToFrames(int cw, int ch, bool skipEmpty);   // corta el frame actual en celdas de cw x ch: un frame por celda
    bool selActive() const { return sel.active; }
    SelRect selection() const { return sel; }
    // exportar: layout 0 frame actual, 1 hoja horizontal, 2 hoja vertical, 3 hoja en cuadricula, 4 un archivo por frame
    bool exportFile(const std::string &path, int layout, bool withJson, std::string &err) const;
    std::vector<Px> sheet(int layout, int &W, int &H, std::vector<SelRect> *cells = nullptr) const;
    bool loadImageFile(const std::string &path, std::string &err);   // .png .gif (animado) .bmp .jpg .tga .qoi: un sprite nuevo
    std::function<void(const std::string &)> onStatus;   // avisos para la barra de notificaciones del editor
    bool celEmpty(int layer, int frame) const;
    bool dirtyForProject = false;
    bool scriptHold = false;                   // pruebas: el trazo sigue como con el boton apretado (strokehold)              // hay cambios que volcar al proyecto

private:
    std::vector<SpriteState> undoStack, redoStack;
    // vista
    float zoom = 0, panX = 0, panY = 0;
    bool fitPending = true;
    // trazo en curso
    bool stroking = false, strokeSecondary = false, panning = false;
    int sx = 0, sy = 0, lx = 0, ly = 0;
    void drawPresence(ImDrawList *dl, ImVec2 o, bool hovered, int px, int py);   // trabajar juntos
    std::vector<Px> preview;                  // capa temporal para lineas/figuras/mover
    std::vector<int> ppPts;                   // pixel perfect: indices recientes
    SelRect sel, selStart;
    std::vector<uint8_t> selMask, selPrev;    // seleccion por pixeles (w*h) y la de antes del trazo en curso (para sumar o restar)
    int selMode = 0, moveDX = 0, moveDY = 0;
    std::vector<std::pair<int, int>> lassoPts;
    bool selHas(int i) const { return !sel.active || (i >= 0 && i < (int)selMask.size() && selMask[i]); }
    void setSelection(const std::vector<uint8_t> &m, int mode);
    void dropSelection() { sel.active = false; selMask.clear(); }
    void cropTo(int x, int y, int w, int h);
    void drawSelectionOutline(ImDrawList *dl, ImVec2 o);
    void drawPaletteFileButtons(Project &proj);
    std::vector<Px> floatPx; int floatW = 0, floatH = 0, floatX = 0, floatY = 0; bool floating = false;
    // animacion
    bool playing = false; double playT = 0;
    // texturas
    ui::PixTexture texCanvas, texPrev, texNext, texAnim;
    std::vector<Px> scratch;
    char renameBuf[64] = {0}; int renaming = -1;
    float tlH = -1;                            // alto de la linea de tiempo (se puede arrastrar)
    int groupLast[8] = {0};                    // herramienta elegida en cada grupo de la barra
    int fxKind = -1; bool fxAll = false; float fxA = 0, fxB = 0; Px fxC1{0, 0, 0, 255}, fxC2{255, 255, 255, 255};
    void drawFxPopup();
    int newW = 0, newH = 0; bool resizeCenter = true;

    std::vector<Px> &cel() { return st.layers[curLayer].cels[curFrame]; }
    bool canPaint();
    void plot(std::vector<Px> &dst, int x, int y, Px c, bool blendOver);
    void stamp(std::vector<Px> &dst, int x, int y, Px c);
    void lineTo(std::vector<Px> &dst, int x0, int y0, int x1, int y1, Px c);
    void shapeTo(std::vector<Px> &dst, int x0, int y0, int x1, int y1, Px c, bool ellipse, bool square);
    void fill(int x, int y, Px c);
    void pickAt(int x, int y, bool secondary);
    void commitPreview();
    void drawToolbar();
    void drawOptions();
    void drawCanvas();
    void drawSidePanel(Project &proj);
    void drawTimeline();
    void handleShortcuts();
    Px strokeColor() const;
};
