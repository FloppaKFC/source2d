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
    std::vector<std::vector<Px>> cels;
};

struct SpriteState {
    int w = 16, h = 16;
    std::vector<SpriteLayer> layers;
    std::vector<int> durations;
};

enum class Tool { Pencil, Eraser, Bucket, Picker, Line, Rect, Ellipse, Select, Move, Hand, Lasso, Wand, Spray, Gradient, Contour, SelEllipse };

struct SpriteToolState {
    Tool tool = Tool::Pencil;
    int brush = 1;
    bool pixelPerfect = false, fillShapes = false, contiguous = true;
    int wandTolerance = 0;
    bool wandContiguous = true;
    int sprayDensity = 6;
    bool gradDither = false;
    bool mirrorX = false, mirrorY = false, onion = false, grid = true;
    Px fg{20, 12, 28, 255}, bg{0, 0, 0, 0};
    std::vector<Px> palette;
    std::vector<Px> clip; int clipW = 0, clipH = 0;
};
SpriteToolState &spriteTools();
void loadPaletteFrom(const json &doc);
void savePaletteTo(json &doc);
bool loadPaletteFile(const std::string &path, std::vector<Px> &out, std::string &err);
bool savePaletteFile(const std::string &path, const std::vector<Px> &pal, std::string &err);
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
    void reload(const json &sprite);
    void draw(Project &proj);
    void drawBody(Project &proj);
    bool tileMode = false;
    ~SpriteEditor();

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
    void scaleSprite(int percent);
    void duplicateLayer();
    void mergeDown();
    void moveFrame(int dir);
    void applyFx(int kind, bool allFrames, float a, float b, Px c1, Px c2);
    void selectAll();
    void clearSelection();
    void copySel(bool cut);
    void paste();
    void composite(int frame, std::vector<Px> &out, bool withPreview) const;
    void selectRect(int x, int y, int w, int h, int mode = 0);
    void selectByColor(int x, int y, bool contiguous, int mode = 0);
    void invertSelection();
    void cropToSelection();
    void trimCanvas();
    void cutToNewLayer();
    void sliceToFrames(int cw, int ch, bool skipEmpty);
    bool selActive() const { return sel.active; }
    SelRect selection() const { return sel; }
    bool exportFile(const std::string &path, int layout, bool withJson, std::string &err) const;
    std::vector<Px> sheet(int layout, int &W, int &H, std::vector<SelRect> *cells = nullptr) const;
    bool loadImageFile(const std::string &path, std::string &err);
    std::function<void(const std::string &)> onStatus;
    bool celEmpty(int layer, int frame) const;
    bool dirtyForProject = false;
    bool scriptHold = false;

private:
    std::vector<SpriteState> undoStack, redoStack;
    float zoom = 0, panX = 0, panY = 0;
    bool fitPending = true;
    bool stroking = false, strokeSecondary = false, panning = false;
    int sx = 0, sy = 0, lx = 0, ly = 0;
    void drawPresence(ImDrawList *dl, ImVec2 o, bool hovered, int px, int py);
    std::vector<Px> preview;
    std::vector<int> ppPts;
    SelRect sel, selStart;
    std::vector<uint8_t> selMask, selPrev;
    int selMode = 0, moveDX = 0, moveDY = 0;
    std::vector<std::pair<int, int>> lassoPts;
    bool selHas(int i) const { return !sel.active || (i >= 0 && i < (int)selMask.size() && selMask[i]); }
    void setSelection(const std::vector<uint8_t> &m, int mode);
    void dropSelection() { sel.active = false; selMask.clear(); }
    void cropTo(int x, int y, int w, int h);
    void drawSelectionOutline(ImDrawList *dl, ImVec2 o);
    void drawPaletteFileButtons(Project &proj);
    std::vector<Px> floatPx; int floatW = 0, floatH = 0, floatX = 0, floatY = 0; bool floating = false;
    bool playing = false; double playT = 0;
    ui::PixTexture texCanvas, texPrev, texNext, texAnim;
    std::vector<Px> scratch;
    char renameBuf[64] = {0}; int renaming = -1;
    float tlH = -1;
    int groupLast[8] = {0};
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
