#include "sprite_editor.hpp"
#include "school.hpp"
#include "collab.hpp"
#include "assets.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "imgui_internal.h"

using ui::Icon;

static const unsigned kDB32[32] = {
    0x000000, 0x222034, 0x45283c, 0x663931, 0x8f563b, 0xdf7126, 0xd9a066, 0xeec39a, 0xfbf236, 0x99e550, 0x6abe30,
    0x37946e, 0x4b692f, 0x524b24, 0x323c39, 0x3f3f74, 0x306082, 0x5b6ee1, 0x639bff, 0x5fcde4, 0xcbdbfc, 0xffffff,
    0x9badb7, 0x847e87, 0x696a6a, 0x595652, 0x76428a, 0xac3232, 0xd95763, 0xd77bba, 0x8f974a, 0x8a6f30};

SpriteToolState &spriteTools() {
    static SpriteToolState t;
    if (t.palette.empty())
        for (unsigned c : kDB32) t.palette.push_back(Px{(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, 255});
    return t;
}

void loadPaletteFrom(const json &doc) {
    auto &t = spriteTools();
    if (!doc.contains("palette") || !doc["palette"].is_array() || doc["palette"].empty()) return;
    t.palette.clear();
    for (auto &c : doc["palette"]) if (c.is_string()) t.palette.push_back(parseColor(c.get<std::string>()));
}

void savePaletteTo(json &doc) {
    json arr = json::array();
    for (auto &c : spriteTools().palette) arr.push_back(colorToString(c).empty() ? "#00000000" : colorToString(c));
    doc["palette"] = arr;
}

static bool colorSwatch(const char *id, Px &c, float size);
static Px blendOver(Px d, Px s, int extraAlpha = 255) {
    int sa = s.a * extraAlpha / 255;
    if (sa <= 0) return d;
    if (sa >= 255 || d.a == 0) { Px r = s; r.a = (uint8_t)sa; return r; }
    int da = d.a * (255 - sa) / 255, oa = sa + da;
    Px r;
    r.r = (uint8_t)((s.r * sa + d.r * da) / oa);
    r.g = (uint8_t)((s.g * sa + d.g * da) / oa);
    r.b = (uint8_t)((s.b * sa + d.b * da) / oa);
    r.a = (uint8_t)oa;
    return r;
}

static std::vector<Px> pixelsFrom(const json &arr, int n) {
    std::vector<Px> out(n);
    if (!arr.is_array()) return out;
    for (int i = 0; i < n && i < (int)arr.size(); i++) if (arr[i].is_string()) out[i] = parseColor(arr[i].get<std::string>());
    return out;
}

bool SpriteEditor::load(const json &sp) {
    spriteId = sp.value("id", "");
    name = sp.value("name", "Sprite");
    st = SpriteState{};
    st.w = std::max(1, sp.value("width", 16));
    st.h = std::max(1, sp.value("height", 16));
    int n = st.w * st.h;
    int nframes = sp.contains("frames") && sp["frames"].is_array() ? (int)sp["frames"].size() : 0;
    bool layersOk = sp.contains("layers") && sp["layers"].is_array() && !sp["layers"].empty();
    if (layersOk)
        for (auto &L : sp["layers"]) if (!L.contains("frames") || !L["frames"].is_array() || (int)L["frames"].size() != nframes) layersOk = false;
    if (layersOk) {
        for (auto &L : sp["layers"]) {
            SpriteLayer l;
            l.name = L.value("name", "Capa");
            l.visible = L.value("vis", true);
            l.locked = L.value("locked", false);
            l.opacity = std::clamp(L.value("opacity", 255), 0, 255);
            for (auto &f : L["frames"]) l.cels.push_back(pixelsFrom(f, n));
            st.layers.push_back(std::move(l));
        }
    } else {
        SpriteLayer l;
        for (int f = 0; f < nframes; f++) l.cels.push_back(pixelsFrom(sp["frames"][f].contains("pixels") ? sp["frames"][f]["pixels"] : json(), n));
        st.layers.push_back(std::move(l));
    }
    if (st.layers[0].cels.empty()) for (auto &l : st.layers) l.cels.push_back(std::vector<Px>(n));
    int frames = (int)st.layers[0].cels.size();
    st.durations.assign(frames, 100);
    if (sp.contains("durations") && sp["durations"].is_array())
        for (int i = 0; i < frames && i < (int)sp["durations"].size(); i++) st.durations[i] = std::max(10, sp["durations"][i].get<int>());
    curLayer = (int)st.layers.size() - 1; curFrame = 0;
    newW = st.w; newH = st.h;
    return true;
}

void SpriteEditor::reload(const json &sp) {
    int f = curFrame, l = curLayer;
    load(sp);
    curFrame = std::clamp(f, 0, (int)st.layers[0].cels.size() - 1);
    curLayer = std::clamp(l, 0, (int)st.layers.size() - 1);
    redoStack.clear();
}

void SpriteEditor::writeTo(json &sp) const {
    sp["name"] = name;
    sp["width"] = st.w;
    sp["height"] = st.h;
    int frames = (int)st.layers[0].cels.size();
    json fr = json::array();
    std::vector<Px> comp;
    for (int f = 0; f < frames; f++) {
        composite(f, comp, false);
        json px = json::array();
        for (auto &c : comp) px.push_back(colorToString(c));
        fr.push_back({{"pixels", px}});
    }
    sp["frames"] = fr;
    json layers = json::array();
    for (auto &l : st.layers) {
        json lf = json::array();
        for (auto &c : l.cels) {
            json px = json::array();
            for (auto &p : c) px.push_back(colorToString(p));
            lf.push_back(px);
        }
        layers.push_back({{"name", l.name}, {"vis", l.visible}, {"locked", l.locked}, {"opacity", l.opacity}, {"frames", lf}});
    }
    sp["layers"] = layers;
    sp["durations"] = st.durations;
}

SpriteEditor::~SpriteEditor() {
    texCanvas.release(); texPrev.release(); texNext.release(); texAnim.release();
}

void SpriteEditor::composite(int frame, std::vector<Px> &out, bool withPreview) const {
    int n = st.w * st.h;
    out.assign(n, Px{});
    for (int li = 0; li < (int)st.layers.size(); li++) {
        const SpriteLayer &L = st.layers[li];
        if (!L.visible || frame >= (int)L.cels.size()) continue;
        const std::vector<Px> &c = L.cels[frame];
        bool floatHere = withPreview && li == curLayer && frame == curFrame;
        for (int i = 0; i < n; i++) {
            Px p = c[i];
            if (floatHere && !preview.empty()) {
                if (stroking && spriteTools().tool == Tool::Eraser) { if (preview[i].a) p = Px{}; }
                else if (preview[i].a) p = blendOver(p, preview[i]);
            }
            if (p.a) out[i] = blendOver(out[i], p, L.opacity);
        }
    }
}

bool SpriteEditor::celEmpty(int layer, int frame) const {
    for (auto &p : st.layers[layer].cels[frame]) if (p.a) return false;
    return true;
}

void SpriteEditor::pushUndo() {
    undoStack.push_back(st);
    if (undoStack.size() > 100) undoStack.erase(undoStack.begin());
    redoStack.clear();
}
void SpriteEditor::undo() {
    if (undoStack.empty()) { status = "Nada que deshacer"; return; }
    redoStack.push_back(st); st = undoStack.back(); undoStack.pop_back();
    curLayer = std::min(curLayer, (int)st.layers.size() - 1);
    curFrame = std::min(curFrame, (int)st.layers[0].cels.size() - 1);
    newW = st.w; newH = st.h; fitPending = true;
    if (selMask.size() != (size_t)(st.w * st.h)) dropSelection();
    dirtyForProject = true;
}
void SpriteEditor::redo() {
    if (redoStack.empty()) { status = "Nada que rehacer"; return; }
    undoStack.push_back(st); st = redoStack.back(); redoStack.pop_back();
    curLayer = std::min(curLayer, (int)st.layers.size() - 1);
    curFrame = std::min(curFrame, (int)st.layers[0].cels.size() - 1);
    newW = st.w; newH = st.h; fitPending = true;
    if (selMask.size() != (size_t)(st.w * st.h)) dropSelection();
    dirtyForProject = true;
}

bool SpriteEditor::canPaint() {
    SpriteLayer &L = st.layers[curLayer];
    if (L.locked) { status = "La capa \"" + L.name + "\" está bloqueada"; return false; }
    if (!L.visible) { status = "La capa \"" + L.name + "\" está oculta"; return false; }
    return true;
}

void SpriteEditor::plot(std::vector<Px> &dst, int x, int y, Px c, bool over) {
    if (x < 0 || y < 0 || x >= st.w || y >= st.h) return;
    if (!selHas(y * st.w + x)) return;
    Px &d = dst[y * st.w + x];
    d = over && c.a < 255 ? blendOver(d, c) : c;
}

void SpriteEditor::stamp(std::vector<Px> &dst, int x, int y, Px c) {
    auto &t = spriteTools();
    int b = std::max(1, t.brush), o = (b - 1) / 2;
    auto put = [&](int px, int py) {
        for (int yy = 0; yy < b; yy++) for (int xx = 0; xx < b; xx++) plot(dst, px - o + xx, py - o + yy, c, false);
    };
    put(x, y);
    if (t.mirrorX) put(st.w - 1 - x, y);
    if (t.mirrorY) put(x, st.h - 1 - y);
    if (t.mirrorX && t.mirrorY) put(st.w - 1 - x, st.h - 1 - y);
}

void SpriteEditor::lineTo(std::vector<Px> &dst, int x0, int y0, int x1, int y1, Px c) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sxs = x0 < x1 ? 1 : -1, sys = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        stamp(dst, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sxs; }
        if (e2 <= dx) { err += dx; y0 += sys; }
    }
}

void SpriteEditor::shapeTo(std::vector<Px> &dst, int x0, int y0, int x1, int y1, Px c, bool ellipse, bool square) {
    if (square) {
        int d = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        x1 = x0 + (x1 >= x0 ? d : -d); y1 = y0 + (y1 >= y0 ? d : -d);
    }
    int ax = std::min(x0, x1), bx = std::max(x0, x1), ay = std::min(y0, y1), by = std::max(y0, y1);
    bool filled = spriteTools().fillShapes;
    if (!ellipse) {
        if (filled) { for (int y = ay; y <= by; y++) for (int x = ax; x <= bx; x++) stamp(dst, x, y, c); }
        else { lineTo(dst, ax, ay, bx, ay, c); lineTo(dst, ax, by, bx, by, c); lineTo(dst, ax, ay, ax, by, c); lineTo(dst, bx, ay, bx, by, c); }
        return;
    }
    double cx = (ax + bx) / 2.0, cy = (ay + by) / 2.0, rx = (bx - ax) / 2.0 + 0.5, ry = (by - ay) / 2.0 + 0.5;
    for (int y = ay; y <= by; y++) for (int x = ax; x <= bx; x++) {
        double nx = (x - cx) / rx, ny = (y - cy) / ry, d = nx * nx + ny * ny;
        if (d > 1.0) continue;
        if (filled) { stamp(dst, x, y, c); continue; }
        bool edge = false;
        for (int k = 0; k < 4 && !edge; k++) {
            int qx = x + (k == 0) - (k == 1), qy = y + (k == 2) - (k == 3);
            double mx = (qx - cx) / rx, my = (qy - cy) / ry;
            if (mx * mx + my * my > 1.0) edge = true;
        }
        if (edge) stamp(dst, x, y, c);
    }
}

void SpriteEditor::fill(int x, int y, Px c) {
    if (x < 0 || y < 0 || x >= st.w || y >= st.h) return;
    std::vector<Px> &d = cel();
    Px target = d[y * st.w + x];
    auto inSel = [&](int px, int py) { return selHas(py * st.w + px); };
    if (!spriteTools().contiguous) {
        for (int yy = 0; yy < st.h; yy++) for (int xx = 0; xx < st.w; xx++) if (inSel(xx, yy) && d[yy * st.w + xx] == target) d[yy * st.w + xx] = c;
        return;
    }
    if (target == c) return;
    std::vector<int> stack{y * st.w + x};
    while (!stack.empty()) {
        int i = stack.back(); stack.pop_back();
        int px = i % st.w, py = i / st.w;
        if (d[i] != target || !inSel(px, py)) continue;
        d[i] = c;
        if (px > 0) stack.push_back(i - 1);
        if (px < st.w - 1) stack.push_back(i + 1);
        if (py > 0) stack.push_back(i - st.w);
        if (py < st.h - 1) stack.push_back(i + st.w);
    }
}

void SpriteEditor::pickAt(int x, int y, bool secondary) {
    if (x < 0 || y < 0 || x >= st.w || y >= st.h) return;
    std::vector<Px> comp;
    composite(curFrame, comp, false);
    (secondary ? spriteTools().bg : spriteTools().fg) = comp[y * st.w + x];
}

Px SpriteEditor::strokeColor() const { return strokeSecondary ? spriteTools().bg : spriteTools().fg; }

void SpriteEditor::strokeBegin(int x, int y, bool secondary) {
    auto &t = spriteTools();
    strokeSecondary = secondary; sx = lx = x; sy = ly = y;
    int n = st.w * st.h;
    switch (t.tool) {
    case Tool::Picker: pickAt(x, y, secondary); return;
    case Tool::Hand: return;
    case Tool::Select: case Tool::Lasso: case Tool::Wand: case Tool::SelEllipse: {
        if (floating) commitPreview();
        bool ig = ImGui::GetCurrentContext() != nullptr;
        selMode = ig && ImGui::GetIO().KeyShift ? 1 : ig && ImGui::GetIO().KeyAlt ? 2 : 0;
        if (t.tool == Tool::Wand) { selectByColor(x, y, t.wandContiguous, selMode); return; }
        selPrev = sel.active && selMask.size() == (size_t)n ? selMask : std::vector<uint8_t>();
        lassoPts.assign(1, {x, y});
        stroking = true;
        return;
    }
    case Tool::Move: {
        if (!canPaint()) return;
        pushUndo();
        stroking = true;
        SelRect r = sel.active ? sel : SelRect{0, 0, st.w, st.h, false};
        floatW = r.w; floatH = r.h; floatX = r.x; floatY = r.y; moveDX = moveDY = 0;
        floatPx.assign(floatW * floatH, Px{});
        std::vector<Px> &d = cel();
        for (int yy = 0; yy < r.h; yy++) for (int xx = 0; xx < r.w; xx++) {
            int i = (r.y + yy) * st.w + r.x + xx;
            if (!selHas(i)) continue;
            floatPx[yy * floatW + xx] = d[i]; d[i] = Px{};
        }
        floating = true; selStart = sel;
        preview.assign(n, Px{});
        return;
    }
    case Tool::Bucket:
        if (!canPaint()) return;
        pushUndo(); fill(x, y, strokeColor()); dirtyForProject = true; return;
    default: break;
    }
    if (!canPaint()) return;
    pushUndo();
    stroking = true;
    preview.assign(n, Px{});
    ppPts.clear();
    if (t.tool == Tool::Contour) { lassoPts.assign(1, {x, y}); return; }
    if (t.tool == Tool::Pencil || t.tool == Tool::Eraser) {
        Px c = t.tool == Tool::Eraser ? Px{255, 255, 255, 255} : strokeColor();
        if (t.tool == Tool::Pencil && c.a == 0) { t.tool = Tool::Eraser; c = Px{255, 255, 255, 255}; strokeSecondary = true; ppPts.push_back(-1); }
        stamp(preview, x, y, c);
        if (t.brush == 1) ppPts.push_back(y * st.w + x);
    } else strokeMove(x, y);
}

void SpriteEditor::strokeMove(int x, int y) {
    if (!stroking) return;
    auto &t = spriteTools();
    bool shift = ImGui::GetCurrentContext() && ImGui::GetIO().KeyShift;
    switch (t.tool) {
    case Tool::Pencil: case Tool::Eraser: {
        if (x == lx && y == ly) return;
        Px c = t.tool == Tool::Eraser ? Px{255, 255, 255, 255} : strokeColor();
        if (t.pixelPerfect && t.brush == 1 && !t.mirrorX && !t.mirrorY) {
            int dx = std::abs(x - lx), dy = -std::abs(y - ly), sxs = lx < x ? 1 : -1, sys = ly < y ? 1 : -1, err = dx + dy, cx = lx, cy = ly;
            while (cx != x || cy != y) {
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; cx += sxs; }
                if (e2 <= dx) { err += dx; cy += sys; }
                if (cx < 0 || cy < 0 || cx >= st.w || cy >= st.h) continue;
                int idx = cy * st.w + cx;
                plot(preview, cx, cy, c, false);
                ppPts.push_back(idx);
                size_t m = ppPts.size();
                if (m >= 3 && ppPts[m - 3] >= 0) {
                    int a = ppPts[m - 3], b = ppPts[m - 2], d = ppPts[m - 1];
                    int ax = a % st.w, ay = a / st.w, bx = b % st.w, by = b / st.w, ddx = d % st.w, ddy = d / st.w;
                    bool corner = std::abs(ax - ddx) == 1 && std::abs(ay - ddy) == 1 && (std::abs(ax - bx) + std::abs(ay - by) == 1);
                    if (corner) { preview[b] = Px{}; ppPts.erase(ppPts.end() - 2); }
                }
            }
        } else lineTo(preview, lx, ly, x, y, c);
        break;
    }
    case Tool::Line: case Tool::Rect: case Tool::Ellipse: {
        std::fill(preview.begin(), preview.end(), Px{});
        Px c = strokeColor();
        if (c.a == 0) c = Px{0, 0, 0, 0};
        int ex = x, ey = y;
        if (t.tool == Tool::Line) {
            if (shift) {
                int ddx = x - sx, ddy = y - sy;
                if (std::abs(ddx) > 2 * std::abs(ddy)) ey = sy;
                else if (std::abs(ddy) > 2 * std::abs(ddx)) ex = sx;
                else { int d = std::max(std::abs(ddx), std::abs(ddy)); ex = sx + (ddx < 0 ? -d : d); ey = sy + (ddy < 0 ? -d : d); }
            }
            lineTo(preview, sx, sy, ex, ey, c.a ? c : Px{255, 255, 255, 1});
        } else shapeTo(preview, sx, sy, ex, ey, c.a ? c : Px{255, 255, 255, 1}, t.tool == Tool::Ellipse, shift);
        break;
    }
    case Tool::Select: {
        int ax = std::clamp(std::min(sx, x), 0, st.w - 1), ay = std::clamp(std::min(sy, y), 0, st.h - 1);
        int bx = std::clamp(std::max(sx, x), 0, st.w - 1), by = std::clamp(std::max(sy, y), 0, st.h - 1);
        std::vector<uint8_t> m(st.w * st.h, 0);
        for (int yy = ay; yy <= by; yy++) for (int xx = ax; xx <= bx; xx++) m[yy * st.w + xx] = 1;
        setSelection(m, selMode);
        break;
    }
    case Tool::Lasso:
        if (lassoPts.empty() || lassoPts.back() != std::make_pair(x, y)) lassoPts.push_back({x, y});
        break;
    case Tool::SelEllipse: {
        int ax = std::min(sx, x), ay = std::min(sy, y), bx = std::max(sx, x), by = std::max(sy, y);
        double cx = (ax + bx) / 2.0, cy = (ay + by) / 2.0, rx = (bx - ax) / 2.0 + 0.5, ry = (by - ay) / 2.0 + 0.5;
        std::vector<uint8_t> m(st.w * st.h, 0);
        for (int yy = std::max(0, ay); yy <= std::min(st.h - 1, by); yy++) for (int xx = std::max(0, ax); xx <= std::min(st.w - 1, bx); xx++) {
            double ddx = (xx - cx) / rx, ddy = (yy - cy) / ry;
            if (ddx * ddx + ddy * ddy <= 1.0) m[yy * st.w + xx] = 1;
        }
        setSelection(m, selMode);
        break;
    }
    case Tool::Spray: {
        Px c = strokeColor();
        int r = std::max(1, t.brush);
        static uint32_t seed = 12345;
        for (int k = 0; k < t.sprayDensity; k++) {
            seed = seed * 1664525u + 1013904223u; double a = (seed >> 8) / 16777216.0 * 6.2831853;
            seed = seed * 1664525u + 1013904223u; double d = std::sqrt((seed >> 8) / 16777216.0) * r;
            int px = x + (int)std::lround(std::cos(a) * d), py = y + (int)std::lround(std::sin(a) * d);
            if (px >= 0 && py >= 0 && px < st.w && py < st.h) plot(preview, px, py, c, false);
        }
        break;
    }
    case Tool::Gradient: {
        Px a = t.fg, b = t.bg;
        double vx = x - sx, vy = y - sy, len2 = vx * vx + vy * vy;
        static const int B4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        for (int yy = 0; yy < st.h; yy++) for (int xx = 0; xx < st.w; xx++) {
            int i = yy * st.w + xx;
            if (!selHas(i)) { preview[i] = Px{}; continue; }
            double k = len2 > 0 ? std::clamp(((xx - sx) * vx + (yy - sy) * vy) / len2, 0.0, 1.0) : 0.0;
            if (t.gradDither) { preview[i] = k * 16 > B4[(yy & 3) * 4 + (xx & 3)] + 0.5 ? b : a; if (!preview[i].a) preview[i] = Px{0, 0, 0, 0}; continue; }
            auto L = [k](int p, int q) { return (uint8_t)std::lround(p + (q - p) * k); };
            preview[i] = Px{L(a.r, b.r), L(a.g, b.g), L(a.b, b.b), L(a.a, b.a)};
        }
        break;
    }
    case Tool::Contour:
        if (lassoPts.empty() || lassoPts.back() != std::make_pair(x, y)) lassoPts.push_back({x, y});
        std::fill(preview.begin(), preview.end(), Px{});
        for (size_t i = 1; i < lassoPts.size(); i++) lineTo(preview, lassoPts[i - 1].first, lassoPts[i - 1].second, lassoPts[i].first, lassoPts[i].second, strokeColor());
        break;
    case Tool::Move: {
        floatX += x - lx; floatY += y - ly; moveDX += x - lx; moveDY += y - ly;
        if (sel.active) { sel.x += x - lx; sel.y += y - ly; }
        std::fill(preview.begin(), preview.end(), Px{});
        for (int yy = 0; yy < floatH; yy++) for (int xx = 0; xx < floatW; xx++) {
            int px = floatX + xx, py = floatY + yy;
            if (px >= 0 && py >= 0 && px < st.w && py < st.h) preview[py * st.w + px] = floatPx[yy * floatW + xx];
        }
        break;
    }
    default: break;
    }
    lx = x; ly = y;
}

void SpriteEditor::commitPreview() {
    std::vector<Px> &d = cel();
    auto &t = spriteTools();
    int n = st.w * st.h;
    if ((int)preview.size() != n) { preview.clear(); return; }
    bool erase = t.tool == Tool::Eraser || ((t.tool == Tool::Line || t.tool == Tool::Rect || t.tool == Tool::Ellipse) && strokeColor().a == 0);
    if (t.tool == Tool::Gradient) {
        for (int i = 0; i < n; i++) if (selHas(i)) d[i] = preview[i];
        preview.clear(); dirtyForProject = true; return;
    }
    if (t.tool == Tool::Move || floating) {
        for (int i = 0; i < n; i++) if (preview[i].a) d[i] = preview[i];
        floating = false; floatPx.clear();
        if (sel.active && selMask.size() == (size_t)n && (moveDX || moveDY)) {
            std::vector<uint8_t> m(n, 0);
            for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++) {
                int tx = x + moveDX, ty = y + moveDY;
                if (selMask[y * st.w + x] && tx >= 0 && ty >= 0 && tx < st.w && ty < st.h) m[ty * st.w + tx] = 1;
            }
            selPrev.clear();
            setSelection(m, 0);
        }
        moveDX = moveDY = 0;
    } else if (erase) {
        for (int i = 0; i < n; i++) if (preview[i].a && selHas(i)) d[i] = Px{};
    } else {
        for (int i = 0; i < n; i++) if (preview[i].a && selHas(i)) d[i] = preview[i].a == 255 ? preview[i] : blendOver(d[i], preview[i]);
    }
    preview.clear();
    dirtyForProject = true;
}

void SpriteEditor::strokeEnd() {
    if (!stroking) return;
    stroking = false;
    auto &t = spriteTools();
    if (t.tool == Tool::Select || t.tool == Tool::SelEllipse) { if (sx == lx && sy == ly && selMode == 0) dropSelection(); return; }
    if (t.tool == Tool::Contour && lassoPts.size() >= 3) {
        size_t k = lassoPts.size();
        Px c = strokeColor();
        for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++) {
            double px = x + 0.5, py = y + 0.5;
            bool in = false;
            for (size_t i = 0, j = k - 1; i < k; j = i++) {
                double xi = lassoPts[i].first + 0.5, yi = lassoPts[i].second + 0.5, xj = lassoPts[j].first + 0.5, yj = lassoPts[j].second + 0.5;
                if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
            }
            if (in) plot(preview, x, y, c, false);
        }
        lassoPts.clear();
    }
    if (t.tool == Tool::Lasso) {
        int n = st.w * st.h;
        std::vector<uint8_t> m(n, 0);
        size_t k = lassoPts.size();
        if (k < 3) { if (selMode == 0) dropSelection(); lassoPts.clear(); return; }
        for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++) {
            double px = x + 0.5, py = y + 0.5;
            bool in = false;
            for (size_t i = 0, j = k - 1; i < k; j = i++) {
                double xi = lassoPts[i].first + 0.5, yi = lassoPts[i].second + 0.5, xj = lassoPts[j].first + 0.5, yj = lassoPts[j].second + 0.5;
                if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
            }
            m[y * st.w + x] = in;
        }
        for (size_t i = 0; i < k; i++) {
            int x0 = lassoPts[i].first, y0 = lassoPts[i].second, x1 = lassoPts[(i + 1) % k].first, y1 = lassoPts[(i + 1) % k].second;
            int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sxs = x0 < x1 ? 1 : -1, sys = y0 < y1 ? 1 : -1, err = dx + dy;
            for (;;) {
                if (x0 >= 0 && y0 >= 0 && x0 < st.w && y0 < st.h) m[y0 * st.w + x0] = 1;
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sxs; }
                if (e2 <= dx) { err += dx; y0 += sys; }
            }
        }
        lassoPts.clear();
        setSelection(m, selMode);
        return;
    }
    commitPreview();
    if (!ppPts.empty() && ppPts[0] == -1) t.tool = Tool::Pencil;
}

void SpriteEditor::addFrame(bool duplicate) {
    pushUndo();
    int at = curFrame + 1, n = st.w * st.h;
    for (auto &L : st.layers) L.cels.insert(L.cels.begin() + at, duplicate ? L.cels[curFrame] : std::vector<Px>(n));
    st.durations.insert(st.durations.begin() + at, st.durations[curFrame]);
    curFrame = at; dirtyForProject = true;
}
void SpriteEditor::deleteFrame() {
    if (st.layers[0].cels.size() <= 1) { status = "El sprite necesita al menos un frame"; return; }
    pushUndo();
    for (auto &L : st.layers) L.cels.erase(L.cels.begin() + curFrame);
    st.durations.erase(st.durations.begin() + curFrame);
    curFrame = std::min(curFrame, (int)st.layers[0].cels.size() - 1); dirtyForProject = true;
}
void SpriteEditor::addLayer() {
    pushUndo();
    SpriteLayer L;
    L.name = "Capa " + std::to_string(st.layers.size() + 1);
    L.cels.assign(st.layers[0].cels.size(), std::vector<Px>(st.w * st.h));
    st.layers.insert(st.layers.begin() + curLayer + 1, std::move(L));
    curLayer++; dirtyForProject = true;
}
void SpriteEditor::deleteLayer() {
    if (st.layers.size() <= 1) { status = "El sprite necesita al menos una capa"; return; }
    pushUndo();
    st.layers.erase(st.layers.begin() + curLayer);
    curLayer = std::min(curLayer, (int)st.layers.size() - 1); dirtyForProject = true;
}
void SpriteEditor::moveLayer(int dir) {
    int to = curLayer + dir;
    if (to < 0 || to >= (int)st.layers.size()) return;
    pushUndo();
    std::swap(st.layers[curLayer], st.layers[to]);
    curLayer = to; dirtyForProject = true;
}
void SpriteEditor::resizeCanvas(int nw, int nh, bool center) {
    nw = std::clamp(nw, 1, 2048); nh = std::clamp(nh, 1, 2048);
    if (nw == st.w && nh == st.h) return;
    pushUndo();
    int ox = center ? (nw - st.w) / 2 : 0, oy = center ? (nh - st.h) / 2 : 0;
    for (auto &L : st.layers) for (auto &c : L.cels) {
        std::vector<Px> nc(nw * nh);
        for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++) {
            int tx = x + ox, ty = y + oy;
            if (tx >= 0 && ty >= 0 && tx < nw && ty < nh) nc[ty * nw + tx] = c[y * st.w + x];
        }
        c = std::move(nc);
    }
    st.w = nw; st.h = nh; newW = nw; newH = nh; dropSelection(); fitPending = true; dirtyForProject = true;
}
void SpriteEditor::flip(bool horizontal) {
    pushUndo();
    for (auto &L : st.layers) for (auto &c : L.cels) {
        std::vector<Px> nc(c.size());
        for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++)
            nc[y * st.w + x] = horizontal ? c[y * st.w + (st.w - 1 - x)] : c[(st.h - 1 - y) * st.w + x];
        c = std::move(nc);
    }
    dirtyForProject = true;
}
void SpriteEditor::rotate90() {
    pushUndo();
    int nw = st.h, nh = st.w;
    for (auto &L : st.layers) for (auto &c : L.cels) {
        std::vector<Px> nc(c.size());
        for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++) nc[x * nw + (nw - 1 - y)] = c[y * st.w + x];
        c = std::move(nc);
    }
    st.w = nw; st.h = nh; newW = nw; newH = nh; dropSelection(); fitPending = true; dirtyForProject = true;
}
void SpriteEditor::rotate180() { flip(true); flip(false); }
void SpriteEditor::rotate90ccw() { rotate90(); rotate90(); rotate90(); }
void SpriteEditor::scaleSprite(int percent) {
    percent = std::clamp(percent, 10, 1600);
    int nw = std::clamp(st.w * percent / 100, 1, 2048), nh = std::clamp(st.h * percent / 100, 1, 2048);
    if (nw == st.w && nh == st.h) return;
    pushUndo();
    for (auto &L : st.layers) for (auto &c : L.cels) {
        std::vector<Px> nc(nw * nh);
        for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) nc[y * nw + x] = c[(y * st.h / nh) * st.w + x * st.w / nw];
        c = std::move(nc);
    }
    st.w = nw; st.h = nh; newW = nw; newH = nh; dropSelection(); fitPending = true; dirtyForProject = true;
}
void SpriteEditor::duplicateLayer() {
    pushUndo();
    SpriteLayer L = st.layers[curLayer];
    L.name += " copia";
    st.layers.insert(st.layers.begin() + curLayer + 1, std::move(L));
    curLayer++; dirtyForProject = true;
}
void SpriteEditor::mergeDown() {
    if (curLayer == 0) { status = "No hay ninguna capa debajo"; return; }
    pushUndo();
    SpriteLayer &top = st.layers[curLayer], &bot = st.layers[curLayer - 1];
    for (size_t f = 0; f < top.cels.size(); f++)
        for (size_t i = 0; i < top.cels[f].size(); i++) {
            Px p = top.cels[f][i];
            if (!p.a || !top.visible) continue;
            p.a = (uint8_t)(p.a * top.opacity / 255);
            bot.cels[f][i] = blendOver(bot.cels[f][i], p);
        }
    st.layers.erase(st.layers.begin() + curLayer);
    curLayer--; dirtyForProject = true;
}
void SpriteEditor::moveFrame(int dir) {
    int to = curFrame + dir, n = (int)st.layers[0].cels.size();
    if (to < 0 || to >= n) return;
    pushUndo();
    for (auto &L : st.layers) std::swap(L.cels[curFrame], L.cels[to]);
    std::swap(st.durations[curFrame], st.durations[to]);
    curFrame = to; dirtyForProject = true;
}
void SpriteEditor::applyFx(int kind, bool allFrames, float a, float b, Px c1, Px c2) {
    if (!canPaint()) return;
    pushUndo();
    int f0 = allFrames ? 0 : curFrame, f1 = allFrames ? (int)st.layers[curLayer].cels.size() - 1 : curFrame;
    for (int f = f0; f <= f1; f++) {
        std::vector<Px> &d = st.layers[curLayer].cels[f], src = d;
        for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++) {
            int i = y * st.w + x;
            if (!selHas(i)) continue;
            Px p = src[i];
            switch (kind) {
            case 0: {
                if (p.a) break;
                bool edge = false;
                for (int k = 0; k < 4 && !edge; k++) {
                    int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
                    if (nx >= 0 && ny >= 0 && nx < st.w && ny < st.h && src[ny * st.w + nx].a) edge = true;
                }
                if (edge) d[i] = c1;
                break;
            }
            case 1: if (p.a) d[i] = Px{(uint8_t)(255 - p.r), (uint8_t)(255 - p.g), (uint8_t)(255 - p.b), p.a}; break;
            case 2: if (p.a) { uint8_t g = (uint8_t)std::lround(0.299 * p.r + 0.587 * p.g + 0.114 * p.b); d[i] = Px{g, g, g, p.a}; } break;
            case 3: if (p.a) {
                double k = (100.0 + b) / 100.0;
                auto C = [&](int v) { return (uint8_t)std::clamp((int)std::lround((v - 128) * k + 128 + a * 2.55), 0, 255); };
                d[i] = Px{C(p.r), C(p.g), C(p.b), p.a};
                } break;
            case 4: if (p.a) {
                double r = p.r / 255.0, g = p.g / 255.0, bl = p.b / 255.0, mx = std::max({r, g, bl}), mn = std::min({r, g, bl}), l = (mx + mn) / 2, h = 0, s = 0;
                if (mx != mn) {
                    double dd = mx - mn; s = l > 0.5 ? dd / (2 - mx - mn) : dd / (mx + mn);
                    h = mx == r ? (g - bl) / dd + (g < bl ? 6 : 0) : mx == g ? (bl - r) / dd + 2 : (r - g) / dd + 4; h /= 6;
                }
                h = std::fmod(h + a / 360.0 + 1.0, 1.0); s = std::clamp(s * (1 + b / 100.0), 0.0, 1.0);
                auto hue = [](double p2, double q, double t) { if (t < 0) t += 1; if (t > 1) t -= 1; if (t < 1.0 / 6) return p2 + (q - p2) * 6 * t;
                                                               if (t < 0.5) return q; if (t < 2.0 / 3) return p2 + (q - p2) * (2.0 / 3 - t) * 6; return p2; };
                double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p2 = 2 * l - q;
                double R = s ? hue(p2, q, h + 1.0 / 3) : l, G = s ? hue(p2, q, h) : l, Bc = s ? hue(p2, q, h - 1.0 / 3) : l;
                d[i] = Px{(uint8_t)std::lround(R * 255), (uint8_t)std::lround(G * 255), (uint8_t)std::lround(Bc * 255), p.a};
                } break;
            case 5: if (p == c1) d[i] = c2; break;
            case 6: { int sx2 = ((x - (int)a) % st.w + st.w) % st.w, sy2 = ((y - (int)b) % st.h + st.h) % st.h; d[i] = src[sy2 * st.w + sx2]; break; }
            case 7: if (p.a) {
                bool alone = true;
                for (int yy = -1; yy <= 1 && alone; yy++) for (int xx = -1; xx <= 1; xx++) {
                    if (!xx && !yy) continue;
                    int nx = x + xx, ny = y + yy;
                    if (nx >= 0 && ny >= 0 && nx < st.w && ny < st.h && src[ny * st.w + nx].a) { alone = false; break; }
                }
                if (alone) d[i] = Px{};
                } break;
            }
        }
    }
    dirtyForProject = true;
}
void SpriteEditor::drawFxPopup() {
    static const char *names[] = {"Contorno", "Invertir colores", "Escala de grises", "Brillo y contraste", "Tono y saturación",
                                  "Reemplazar color", "Desplazar", "Quitar píxeles sueltos"};
    if (fxKind >= 0 && !ImGui::IsPopupOpen("##fx")) ImGui::OpenPopup("##fx");
    if (ImGui::BeginPopup("##fx")) {
        ImGui::SeparatorText(names[std::clamp(fxKind, 0, 7)]);
        if (fxKind == 0 || fxKind == 5) { ImGui::Text(fxKind == 0 ? "Color del contorno" : "Cambiar este color"); ImGui::SameLine(); colorSwatch("##c1", fxC1, 22); ImGui::SameLine(); if (ImGui::SmallButton("Principal")) fxC1 = spriteTools().fg; }
        if (fxKind == 5) { ImGui::Text("Por este"); ImGui::SameLine(); colorSwatch("##c2", fxC2, 22); ImGui::SameLine(); if (ImGui::SmallButton("Secundario")) fxC2 = spriteTools().bg; }
        if (fxKind == 3) { ImGui::SliderFloat("Brillo", &fxA, -100, 100, "%.0f"); ImGui::SliderFloat("Contraste", &fxB, -100, 100, "%.0f"); }
        if (fxKind == 4) { ImGui::SliderFloat("Tono", &fxA, -180, 180, "%.0f°"); ImGui::SliderFloat("Saturación", &fxB, -100, 100, "%.0f"); }
        if (fxKind == 6) { ImGui::DragFloat("X", &fxA, 0.2f, -2048, 2048, "%.0f px"); ImGui::DragFloat("Y", &fxB, 0.2f, -2048, 2048, "%.0f px"); }
        ImGui::Checkbox("Todos los frames", &fxAll);
        ImGui::TextDisabled(sel.active ? "Solo en la selección, en la capa actual." : "En la capa actual.");
        if (ui::primaryButton("Aplicar", ImVec2(110, 0))) { applyFx(fxKind, fxAll, fxA, fxB, fxC1, fxC2); fxKind = -1; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) { fxKind = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    } else fxKind = -1;
}
void SpriteEditor::selectAll() { selectRect(0, 0, st.w, st.h, 0); }
void SpriteEditor::clearSelection() {
    if (!canPaint()) return;
    pushUndo();
    SelRect r = sel.active ? sel : SelRect{0, 0, st.w, st.h, true};
    for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++)
        if (x >= 0 && y >= 0 && x < st.w && y < st.h && selHas(y * st.w + x)) cel()[y * st.w + x] = Px{};
    dirtyForProject = true;
}
void SpriteEditor::copySel(bool cut) {
    auto &t = spriteTools();
    SelRect r = sel.active ? sel : SelRect{0, 0, st.w, st.h, true};
    t.clipW = r.w; t.clipH = r.h; t.clip.assign(r.w * r.h, Px{});
    for (int y = 0; y < r.h; y++) for (int x = 0; x < r.w; x++) {
        int px = r.x + x, py = r.y + y;
        if (px >= 0 && py >= 0 && px < st.w && py < st.h && selHas(py * st.w + px)) t.clip[y * r.w + x] = cel()[py * st.w + px];
    }
    status = "Copiado " + std::to_string(r.w) + "x" + std::to_string(r.h);
    if (cut) clearSelection();
}
void SpriteEditor::paste() {
    auto &t = spriteTools();
    if (t.clip.empty() || !canPaint()) return;
    pushUndo();
    int ox = sel.active ? sel.x : 0, oy = sel.active ? sel.y : 0;
    for (int y = 0; y < t.clipH; y++) for (int x = 0; x < t.clipW; x++) {
        int px = ox + x, py = oy + y;
        Px c = t.clip[y * t.clipW + x];
        if (c.a && px >= 0 && py >= 0 && px < st.w && py < st.h) cel()[py * st.w + px] = c;
    }
    selectRect(ox, oy, t.clipW, t.clipH, 0);
    t.tool = Tool::Move;
    status = "Pegado: arrastra con Mover para colocarlo";
    dirtyForProject = true;
}

struct ToolInfo { Tool t; Icon ic; const char *name; ImGuiKey key; };
static const ToolInfo kTools[] = {
    {Tool::Select, Icon::Select, "Selección rectangular (M)\nMayús: sumar  Alt: restar", ImGuiKey_M},
    {Tool::Lasso, Icon::Lasso, "Lazo: selección a mano alzada (Q)\nMayús: sumar  Alt: restar", ImGuiKey_Q},
    {Tool::Wand, Icon::Wand, "Varita mágica: selecciona por color (W)\nMayús: sumar  Alt: restar", ImGuiKey_W},
    {Tool::Move, Icon::Move, "Mover (V)", ImGuiKey_V},
    {Tool::Pencil, Icon::Pencil, "Lápiz (B)", ImGuiKey_B}, {Tool::Eraser, Icon::Eraser, "Goma (E)", ImGuiKey_E},
    {Tool::Bucket, Icon::Bucket, "Cubo de pintura (G)", ImGuiKey_G}, {Tool::Picker, Icon::Picker, "Cuentagotas (I)", ImGuiKey_I},
    {Tool::Line, Icon::Line, "Línea (L)", ImGuiKey_L}, {Tool::Rect, Icon::Rect, "Rectángulo (U)", ImGuiKey_U},
    {Tool::Ellipse, Icon::Ellipse, "Elipse (O)", ImGuiKey_O}, {Tool::Hand, Icon::Hand, "Mano (H)", ImGuiKey_H},
    {Tool::SelEllipse, Icon::SelEllipse, "Selección elíptica (Mayús+M)\nMayús: sumar  Alt: restar", ImGuiKey_None},
    {Tool::Spray, Icon::Spray, "Aerógrafo (Mayús+B)", ImGuiKey_None},
    {Tool::Gradient, Icon::Gradient, "Degradado (Mayús+G): del color principal al secundario", ImGuiKey_None},
    {Tool::Contour, Icon::Contour, "Contorno: dibuja una forma a mano y se rellena (D)", ImGuiKey_D},
};
struct ToolGroup { const char *name; std::vector<Tool> tools; };
static const ToolGroup kGroups[] = {
    {"Selección", {Tool::Select, Tool::SelEllipse, Tool::Lasso, Tool::Wand}},
    {"Mover", {Tool::Move}},
    {"Pintar", {Tool::Pencil, Tool::Spray}},
    {"Borrar", {Tool::Eraser}},
    {"Rellenar", {Tool::Bucket, Tool::Gradient}},
    {"Cuentagotas", {Tool::Picker}},
    {"Formas", {Tool::Line, Tool::Rect, Tool::Ellipse, Tool::Contour}},
    {"Mano", {Tool::Hand}},
};
static const ToolInfo *toolInfo(Tool t) { for (auto &ti : kTools) if (ti.t == t) return &ti; return &kTools[0]; }

void SpriteEditor::drawToolbar() {
    auto &t = spriteTools();
    for (int g = 0; g < (int)(sizeof kGroups / sizeof kGroups[0]); g++) {
        const ToolGroup &G = kGroups[g];
        int active = -1;
        for (int k = 0; k < (int)G.tools.size(); k++) if (G.tools[k] == t.tool) active = k;
        if (active >= 0) groupLast[g] = active;
        int show = std::clamp(groupLast[g], 0, (int)G.tools.size() - 1);
        const ToolInfo *ti = toolInfo(G.tools[show]);
        ImGui::PushID(g);
        bool clicked = ui::iconButton("##g", ti->ic, active >= 0, ti->name, 32);
        if (g == 2) school::anchor("sprite.pencil");
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        bool many = G.tools.size() > 1;
        if (many) ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(mx.x - 2, mx.y - 8), ImVec2(mx.x - 2, mx.y - 2), ImVec2(mx.x - 8, mx.y - 2), ui::kText2);
        if (clicked) {
            if (floating) commitPreview();
            if (active >= 0 && many) ImGui::OpenPopup("##grp");
            else t.tool = G.tools[show];
        }
        if (many && ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##grp");
        ImGui::SetNextWindowPos(ImVec2(mx.x + 4, mn.y));
        if (ImGui::BeginPopup("##grp")) {
            for (int k = 0; k < (int)G.tools.size(); k++) {
                const ToolInfo *tk = toolInfo(G.tools[k]);
                ImGui::PushID(k);
                ImVec2 p = ImGui::GetCursorScreenPos();
                std::string nm = tk->name; nm = nm.substr(0, nm.find('\n'));
                if (ImGui::Selectable("##it", t.tool == G.tools[k], 0, ImVec2(260, 26))) { if (floating) commitPreview(); t.tool = G.tools[k]; groupLast[g] = k; }
                ui::drawIcon(ImGui::GetWindowDrawList(), tk->ic, ImVec2(p.x + 13, p.y + 13), 18, ui::kText);
                ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 32, p.y + 5), ui::kText, nm.c_str());
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

void SpriteEditor::drawOptions() {
    auto &t = spriteTools();
    ImGui::AlignTextToFramePadding();
    if (t.tool == Tool::Pencil || t.tool == Tool::Eraser || t.tool == Tool::Line || t.tool == Tool::Rect || t.tool == Tool::Ellipse) {
        ImGui::TextUnformatted("Pincel"); ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderInt("##brush", &t.brush, 1, 16, "%d px");
        ImGui::SameLine();
    }
    if (t.tool == Tool::Pencil || t.tool == Tool::Eraser) { ImGui::Checkbox("Pixel perfect", &t.pixelPerfect); ImGui::SameLine(); }
    if (t.tool == Tool::Spray) {
        ImGui::TextUnformatted("Radio"); ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderInt("##sr", &t.brush, 1, 32, "%d px"); ImGui::SameLine();
        ImGui::TextUnformatted("Densidad"); ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderInt("##sd", &t.sprayDensity, 1, 40); ImGui::SameLine();
    }
    if (t.tool == Tool::Gradient) { ImGui::Checkbox("Tramado (pixel art)", &t.gradDither); ImGui::SameLine(); }
    if (t.tool == Tool::Rect || t.tool == Tool::Ellipse) { ImGui::Checkbox("Relleno", &t.fillShapes); ImGui::SameLine(); }
    if (t.tool == Tool::Bucket) { ImGui::Checkbox("Contiguo", &t.contiguous); ImGui::SameLine(); }
    if (t.tool == Tool::Wand) {
        ImGui::TextUnformatted("Tolerancia"); ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderInt("##tol", &t.wandTolerance, 0, 255);
        ImGui::SameLine();
        ImGui::Checkbox("Contigua", &t.wandContiguous); ImGui::SameLine();
    }
    bool selTool = t.tool == Tool::Select || t.tool == Tool::Lasso || t.tool == Tool::Wand || t.tool == Tool::SelEllipse;
    if (selTool || sel.active) {
        if (ImGui::Button("Todo")) selectAll();
        ImGui::SameLine();
        if (ImGui::Button("Invertir")) { if (floating) commitPreview(); invertSelection(); }
        ImGui::SameLine();
        if (sel.active) {
            if (ImGui::Button("Quitar selección")) { if (floating) commitPreview(); dropSelection(); }
            ImGui::SameLine();
            if (!tileMode) {
                if (ImGui::Button("Recortar lienzo")) { if (floating) commitPreview(); cropToSelection(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("El lienzo pasa a ser el rectángulo de la selección (en todos los frames)");
                ImGui::SameLine();
            }
            if (ImGui::Button("Cortar a capa nueva")) { if (floating) commitPreview(); cutToNewLayer(); }
            ImGui::SameLine();
        }
    }
    ImGui::SameLine(0, 18);
    if (ui::iconButton("mx", Icon::MirrorX, t.mirrorX, "Simetría horizontal", 26)) t.mirrorX = !t.mirrorX;
    ImGui::SameLine();
    if (ui::iconButton("my", Icon::MirrorY, t.mirrorY, "Simetría vertical", 26)) t.mirrorY = !t.mirrorY;
    ImGui::SameLine();
    if (ui::iconButton("grid", Icon::Grid, t.grid, "Cuadrícula de píxeles", 26)) t.grid = !t.grid;
    ImGui::SameLine();
    if (ui::iconButton("onion", Icon::Onion, t.onion, "Onion skin (frames vecinos)", 26)) t.onion = !t.onion;
    ImGui::SameLine();
    ImGui::SameLine(0, 18);
    ImGui::Text("Zoom %d%%", (int)(zoom * 100)); ImGui::SameLine();
    if (ImGui::SmallButton("Ajustar")) fitPending = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("100%")) { zoom = 1; }
}

void SpriteEditor::drawSelectionOutline(ImDrawList *dl, ImVec2 o) {
    if (!sel.active || selMask.size() != (size_t)(st.w * st.h)) return;
    int ox = floating ? moveDX : 0, oy = floating ? moveDY : 0;
    int ph = (int)(ImGui::GetTime() * 6.0) % 2;
    auto has = [&](int x, int y) { return x >= 0 && y >= 0 && x < st.w && y < st.h && selMask[y * st.w + x]; };
    for (int y = std::max(0, sel.y - oy - 1); y <= std::min(st.h - 1, sel.y - oy + sel.h); y++)
        for (int x = std::max(0, sel.x - ox - 1); x <= std::min(st.w - 1, sel.x - ox + sel.w); x++) {
            if (!has(x, y)) continue;
            float X = o.x + (x + ox) * zoom, Y = o.y + (y + oy) * zoom;
            ImU32 c = ((x + y + ph) & 1) ? IM_COL32_WHITE : IM_COL32(0, 0, 0, 255);
            if (!has(x, y - 1)) dl->AddLine(ImVec2(X, Y), ImVec2(X + zoom, Y), c, 1.5f);
            if (!has(x, y + 1)) dl->AddLine(ImVec2(X, Y + zoom), ImVec2(X + zoom, Y + zoom), c, 1.5f);
            if (!has(x - 1, y)) dl->AddLine(ImVec2(X, Y), ImVec2(X, Y + zoom), c, 1.5f);
            if (!has(x + 1, y)) dl->AddLine(ImVec2(X + zoom, Y), ImVec2(X + zoom, Y + zoom), c, 1.5f);
        }
}

void SpriteEditor::drawCanvas() {
    auto &t = spriteTools();
    ImVec2 p0 = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    if (avail.x < 10 || avail.y < 10) return;
    ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), IM_COL32(20, 20, 26, 255));
    if (fitPending || zoom <= 0) {
        zoom = std::max(1.0f, std::floor(std::min((avail.x - 40) / st.w, (avail.y - 40) / st.h)));
        panX = (avail.x - st.w * zoom) / 2; panY = (avail.y - st.h * zoom) / 2;
        fitPending = false;
    }
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 m = io.MousePos;
    if (hovered && io.MouseWheel != 0) {
        float old = zoom;
        zoom = io.MouseWheel > 0 ? std::min(64.0f, zoom < 1 ? zoom * 2 : zoom + std::max(1.0f, std::floor(zoom / 4))) : std::max(0.25f, zoom <= 1 ? zoom / 2 : zoom - std::max(1.0f, std::floor(zoom / 5)));
        float mx = m.x - p0.x, my = m.y - p0.y;
        panX = mx - (mx - panX) * zoom / old; panY = my - (my - panY) * zoom / old;
    }
    bool spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space) && hovered;
    if ((hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) || (spaceHeld && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ||
        (hovered && t.tool == Tool::Hand && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) panning = true;
    if (panning) {
        panX += io.MouseDelta.x; panY += io.MouseDelta.y;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) panning = false;
    }
    ImVec2 o(p0.x + panX, p0.y + panY), e(o.x + st.w * zoom, o.y + st.h * zoom);
    int px = (int)std::floor((m.x - o.x) / zoom), py = (int)std::floor((m.y - o.y) / zoom);

    if (!panning) {
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
            strokeBegin(px, py, ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        if (stroking && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right))) strokeMove(px, py);
        if (stroking && !scriptHold && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) strokeEnd();
    }

    dl->PushClipRect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), true);
    ImVec2 ca(std::max(o.x, p0.x), std::max(o.y, p0.y)), cb(std::min(e.x, p0.x + avail.x), std::min(e.y, p0.y + avail.y));
    if (ca.x < cb.x && ca.y < cb.y) {
        dl->PushClipRect(ca, cb, true);
        ui::checkerboard(dl, ImVec2(o.x - std::fmod(o.x - ca.x, 16.0f) * 0, o.y), e, std::max(4.0f, zoom * 2 > 16 ? 8.0f : 4.0f));
        dl->PopClipRect();
    }
    int frames = (int)st.layers[0].cels.size();
    if (t.onion && !playing) {
        if (curFrame > 0) { composite(curFrame - 1, scratch, false); texPrev.update(scratch, st.w, st.h); dl->AddImage(texPrev.id(), o, e, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 120, 120, 90)); }
        if (curFrame < frames - 1) { composite(curFrame + 1, scratch, false); texNext.update(scratch, st.w, st.h); dl->AddImage(texNext.id(), o, e, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(120, 160, 255, 90)); }
    }
    composite(curFrame, scratch, true);
    texCanvas.update(scratch, st.w, st.h);
    dl->AddImage(texCanvas.id(), o, e);
    if (t.grid && zoom >= 6) {
        ImU32 gc = IM_COL32(0, 0, 0, 40);
        for (int x = 1; x < st.w; x++) dl->AddLine(ImVec2(o.x + x * zoom, o.y), ImVec2(o.x + x * zoom, e.y), gc);
        for (int y = 1; y < st.h; y++) dl->AddLine(ImVec2(o.x, o.y + y * zoom), ImVec2(e.x, o.y + y * zoom), gc);
    }
    dl->AddRect(ImVec2(o.x - 1, o.y - 1), ImVec2(e.x + 1, e.y + 1), IM_COL32(90, 90, 110, 255));
    if (t.mirrorX) dl->AddLine(ImVec2(o.x + st.w * zoom / 2, o.y), ImVec2(o.x + st.w * zoom / 2, e.y), IM_COL32(157, 125, 255, 160), 1.5f);
    if (t.mirrorY) dl->AddLine(ImVec2(o.x, o.y + st.h * zoom / 2), ImVec2(e.x, o.y + st.h * zoom / 2), IM_COL32(157, 125, 255, 160), 1.5f);
    drawSelectionOutline(dl, o);
    if (stroking && t.tool == Tool::Lasso && lassoPts.size() > 1) {
        std::vector<ImVec2> pts;
        for (auto &p : lassoPts) pts.push_back(ImVec2(o.x + (p.first + 0.5f) * zoom, o.y + (p.second + 0.5f) * zoom));
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(0, 0, 0, 255), 0, 3.0f);
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32_WHITE, 0, 1.5f);
    }
    if (hovered && !panning && px >= 0 && py >= 0 && px < st.w && py < st.h &&
        (t.tool == Tool::Pencil || t.tool == Tool::Eraser || t.tool == Tool::Line || t.tool == Tool::Rect || t.tool == Tool::Ellipse || t.tool == Tool::Bucket || t.tool == Tool::Picker)) {
        int b = (t.tool == Tool::Bucket || t.tool == Tool::Picker) ? 1 : t.brush, off = (b - 1) / 2;
        ImVec2 a(o.x + (px - off) * zoom, o.y + (py - off) * zoom);
        dl->AddRect(a, ImVec2(a.x + b * zoom, a.y + b * zoom), IM_COL32(255, 255, 255, 200), 0, 0, 1.5f);
        dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(a.x + b * zoom + 1, a.y + b * zoom + 1), IM_COL32(0, 0, 0, 160));
    }
    drawPresence(dl, o, hovered, px, py);
    dl->PopClipRect();
    if (hovered && px >= 0 && py >= 0 && px < st.w && py < st.h) {
        char buf[64]; snprintf(buf, sizeof buf, "%d, %d", px, py);
        dl->AddText(ImVec2(p0.x + 8, p0.y + avail.y - 22), IM_COL32(200, 200, 214, 255), buf);
    }
}

void SpriteEditor::drawPresence(ImDrawList *dl, ImVec2 o, bool hovered, int px, int py) {
    if (collab::role() == collab::Role::None) return;
    const std::string doc = "sprite:" + spriteId;
    for (auto &p : collab::peers()) {
        if (p.doc != doc || !p.extra.is_object() || (int)jnum(p.extra, "f", -1) != curFrame) continue;
        if (p.extra.contains("paint") && p.extra["paint"].is_array()) {
            const json &pp = p.extra["paint"];
            bool er = jbool(p.extra, "er", false);
            for (size_t k = 0; k + 1 < pp.size(); k += 2) {
                int i = pp[k].get<int>();
                if (i < 0 || i >= st.w * st.h) continue;
                uint32_t c = pp[k + 1].get<uint32_t>();
                ImVec2 a(o.x + (i % st.w) * zoom, o.y + (i / st.w) * zoom), b(a.x + zoom, a.y + zoom);
                if (er) dl->AddRect(a, b, p.color);
                else dl->AddRectFilled(a, b, IM_COL32(c >> 24, c >> 16 & 255, c >> 8 & 255, c & 255));
            }
        }
        if (!p.cursor) continue;
        ImVec2 a(o.x + std::floor(p.x) * zoom, o.y + std::floor(p.y) * zoom);
        dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(a.x + zoom + 1, a.y + zoom + 1), p.color, 0, 0, 2.0f);
        ImVec2 ts = ImGui::CalcTextSize(p.name.c_str()), lp(a.x + zoom + 4, a.y + zoom + 2);
        dl->AddRectFilled(lp, ImVec2(lp.x + ts.x + 10, lp.y + ts.y + 4), p.color, 4);
        dl->AddText(ImVec2(lp.x + 5, lp.y + 2), IM_COL32(20, 20, 26, 255), p.name.c_str());
    }
    bool inside = hovered && px >= 0 && py >= 0 && px < st.w && py < st.h;
    if (!inside && !stroking && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    json extra = {{"f", curFrame}};
    auto &t = spriteTools();
    if (stroking && (int)preview.size() == st.w * st.h && t.tool != Tool::Select && t.tool != Tool::Lasso) {
        json pp = json::array();
        for (int i = 0, n = st.w * st.h; i < n && pp.size() < 12000; i++)
            if (preview[i].a) pp.push_back(i), pp.push_back((uint32_t)preview[i].r << 24 | (uint32_t)preview[i].g << 16 | (uint32_t)preview[i].b << 8 | preview[i].a);
        if (!pp.empty()) { extra["paint"] = pp; if (t.tool == Tool::Eraser) extra["er"] = true; }
    }
    collab::setPresence(doc, (float)px, (float)py, inside, {}, extra);
}

static bool colorSwatch(const char *id, Px &c, float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool r = ImGui::InvisibleButton(id, ImVec2(size, size));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ui::checkerboard(dl, p, ImVec2(p.x + size, p.y + size), size / 4);
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), IM_COL32(c.r, c.g, c.b, c.a));
    dl->AddRect(p, ImVec2(p.x + size, p.y + size), IM_COL32(0, 0, 0, 255));
    if (c.a == 0) dl->AddLine(ImVec2(p.x + 2, p.y + size - 2), ImVec2(p.x + size - 2, p.y + 2), IM_COL32(220, 60, 60, 255), 2);
    return r;
}

static void editColorPopup(const char *popup, Px &c) {
    if (ImGui::BeginPopup(popup)) {
        float col[4] = {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
        if (ImGui::ColorPicker4("##pick", col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_AlphaPreviewHalf))
            c = Px{(uint8_t)(col[0] * 255 + 0.5f), (uint8_t)(col[1] * 255 + 0.5f), (uint8_t)(col[2] * 255 + 0.5f), (uint8_t)(col[3] * 255 + 0.5f)};
        if (ImGui::Button("Transparente")) c = Px{};
        ImGui::EndPopup();
    }
}

void SpriteEditor::drawSidePanel(Project &proj) {
    auto &t = spriteTools();
    ImGui::SeparatorText("Color");
    ImVec2 base = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(base.x + 22, base.y + 22));
    if (colorSwatch("##bg", t.bg, 34)) ImGui::OpenPopup("pick_bg");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Color secundario (clic derecho al pintar)");
    ImGui::SetCursorPos(base);
    if (colorSwatch("##fg", t.fg, 34)) ImGui::OpenPopup("pick_fg");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Color principal (clic izquierdo)");
    editColorPopup("pick_fg", t.fg);
    editColorPopup("pick_bg", t.bg);
    ImGui::SetCursorPos(ImVec2(base.x + 66, base.y + 4));
    if (ImGui::SmallButton("Intercambiar (X)")) std::swap(t.fg, t.bg);
    ImGui::SetCursorPos(ImVec2(base.x + 66, base.y + 30));
    std::string hex = colorToString(t.fg);
    char hb[16]; snprintf(hb, sizeof hb, "%s", hex.empty() ? "transparente" : hex.c_str());
    ImGui::SetNextItemWidth(110);
    if (ImGui::InputText("##hex", hb, sizeof hb, ImGuiInputTextFlags_EnterReturnsTrue)) t.fg = parseColor(hb);
    ImGui::SetCursorPos(ImVec2(base.x, base.y + 64));

    ImGui::SeparatorText("Paleta");
    float sw = 18, avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)(avail / (sw + 3)));
    for (int i = 0; i < (int)t.palette.size(); i++) {
        ImGui::PushID(i);
        if (i % cols) ImGui::SameLine(0, 3);
        Px c = t.palette[i];
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##sw", ImVec2(sw, sw));
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + sw, p.y + sw), IM_COL32(c.r, c.g, c.b, c.a ? c.a : 255));
        bool isFg = c == t.fg, isBg = c == t.bg;
        if (isFg || isBg) dl->AddRect(ImVec2(p.x - 1, p.y - 1), ImVec2(p.x + sw + 1, p.y + sw + 1), isFg ? IM_COL32_WHITE : ui::kAccent, 0.0f, 0, 2.0f);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) t.fg = c;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) t.bg = c;
        if (ImGui::BeginPopupContextItem("##swctx", ImGuiPopupFlags_MouseButtonMiddle)) {
            if (ImGui::MenuItem("Quitar de la paleta")) { t.palette.erase(t.palette.begin() + i); savePaletteTo(proj.doc); proj.dirty = true; }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nIzq: principal  Der: secundario  Central: quitar", colorToString(c).c_str());
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Añadir el color principal")) { t.palette.push_back(t.fg); savePaletteTo(proj.doc); proj.dirty = true; }
    drawPaletteFileButtons(proj);

    if (tileMode) return;
    ImGui::SeparatorText("Vista previa");
    int frames = (int)st.layers[0].cels.size();
    int pf = curFrame;
    if (playing && frames > 0) {
        playT += ImGui::GetIO().DeltaTime * 1000.0;
        int total = 0;
        for (int d : st.durations) total += d;
        double tt = std::fmod(playT, std::max(1, total));
        for (pf = 0; pf < frames - 1 && tt >= st.durations[pf]; pf++) tt -= st.durations[pf];
    }
    composite(pf, scratch, pf == curFrame);
    texAnim.update(scratch, st.w, st.h);
    float pw = ImGui::GetContentRegionAvail().x, scale = std::max(1.0f, std::floor(std::min(pw, 160.0f) / std::max(st.w, st.h)));
    ImVec2 pp = ImGui::GetCursorScreenPos();
    ImVec2 pe(pp.x + st.w * scale, pp.y + st.h * scale);
    ui::checkerboard(ImGui::GetWindowDrawList(), pp, pe, 6);
    ImGui::GetWindowDrawList()->AddImage(texAnim.id(), pp, pe);
    ImGui::Dummy(ImVec2(st.w * scale, st.h * scale));

    ImGui::SeparatorText("Sprite");
    char nb[96]; snprintf(nb, sizeof nb, "%s", name.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", nb, sizeof nb, ImGuiInputTextFlags_EnterReturnsTrue) && nb[0]) { name = nb; dirtyForProject = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Nombre del sprite (Enter para aplicar)");
    ImGui::Text("Tamaño: %d x %d", st.w, st.h);
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##nw", &newW, 0); ImGui::SameLine(); ImGui::TextUnformatted("x"); ImGui::SameLine();
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##nh", &newH, 0);
    ImGui::Checkbox("Centrar", &resizeCenter); ImGui::SameLine();
    if (ImGui::Button("Redimensionar")) resizeCanvas(newW, newH, resizeCenter);
    if (ImGui::Button("Voltear H")) flip(true);
    ImGui::SameLine();
    if (ImGui::Button("Voltear V")) flip(false);
    ImGui::SameLine();
    if (ImGui::Button("Rotar 90")) rotate90();
    if (ImGui::Button("Rotar 180")) rotate180();
    ImGui::SameLine();
    if (ImGui::Button("90 a la izquierda")) rotate90ccw();
    static int scalePct = 200;
    ImGui::SetNextItemWidth(90); ImGui::InputInt("##scp", &scalePct, 0); ImGui::SameLine(); ImGui::TextUnformatted("%"); ImGui::SameLine();
    if (ImGui::Button("Escalar sprite")) scaleSprite(scalePct);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cambia el tamaño de todo el dibujo (vecino más cercano: sin difuminar los píxeles)");

    ImGui::SeparatorText("Efectos");
    {
        static const char *fx[] = {"Contorno", "Invertir", "Grises", "Brillo/contraste", "Tono/saturación", "Reemplazar color", "Desplazar", "Píxeles sueltos"};
        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        for (int i = 0; i < 8; i++) {
            if (i % 2) ImGui::SameLine();
            if (ImGui::Button(fx[i], ImVec2(bw, 0))) { fxKind = i; fxA = fxB = 0; if (i == 0) fxC1 = spriteTools().fg; }
        }
        drawFxPopup();
    }

    ImGui::SeparatorText("Cortar");
    if (ImGui::Button("Quitar bordes vacíos", ImVec2(-1, 0))) trimCanvas();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Recorta el lienzo al dibujo (mirando todos los frames y capas)");
    static int sliceW = 16, sliceH = 16;
    static bool sliceSkip = true;
    ImGui::TextDisabled("Hoja a frames: celdas de");
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##slw", &sliceW, 0); ImGui::SameLine(); ImGui::TextUnformatted("x"); ImGui::SameLine();
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##slh", &sliceH, 0);
    sliceW = std::clamp(sliceW, 1, 2048); sliceH = std::clamp(sliceH, 1, 2048);
    ImGui::Checkbox("Saltar celdas vacías", &sliceSkip);
    if (ImGui::Button("Cortar en frames", ImVec2(-1, 0))) sliceToFrames(sliceW, sliceH, sliceSkip);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Divide el frame actual (una hoja de sprites) en un frame por celda");
}

static ui::FileDialog g_palDlg;
void SpriteEditor::drawPaletteFileButtons(Project &proj) {
    auto &t = spriteTools();
    float hw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button("Cargar paleta", ImVec2(hw, 0)))
        g_palDlg.show(ui::FileDialog::Open, "Cargar paleta", kPaletteOpenExt, ui::homeDir(), "", [this, &proj](const std::string &f) {
            std::vector<Px> pal; std::string err;
            if (!loadPaletteFile(f, pal, err)) { status = err; if (onStatus) onStatus(err); return; }
            spriteTools().palette = pal;
            savePaletteTo(proj.doc); proj.dirty = true;
            status = "Paleta cargada: " + std::to_string(pal.size()) + " colores";
            if (onStatus) onStatus(status);
        });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paletas de Aseprite y otros: .gpl .pal .hex .txt .act .ase/.aseprite o una imagen");
    ImGui::SameLine();
    if (ImGui::Button("Guardar paleta", ImVec2(hw, 0)))
        g_palDlg.show(ui::FileDialog::Save, "Guardar paleta", kPaletteSaveExt, ui::homeDir(), "paleta.gpl", [this](const std::string &f) {
            std::string err;
            status = savePaletteFile(f, spriteTools().palette, err) ? "Paleta guardada en " + f : err;
            if (onStatus) onStatus(status);
        });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(".gpl (GIMP, Aseprite), .pal (JASC), .hex (Lospec), .txt (Paint.NET), .act o .png");
    if (ImGui::Button("Colores del sprite", ImVec2(hw, 0))) {
        std::vector<Px> pal;
        for (auto &L : st.layers) for (auto &c : L.cels) for (auto &p : c)
            if (p.a && pal.size() < 256 && std::find(pal.begin(), pal.end(), p) == pal.end()) pal.push_back(p);
        if (pal.empty()) status = "El sprite no tiene colores todavía";
        else { t.palette = pal; savePaletteTo(proj.doc); proj.dirty = true; status = "Paleta con los " + std::to_string(pal.size()) + " colores del sprite"; }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("La paleta pasa a ser los colores que usa este sprite");
    ImGui::SameLine();
    if (ImGui::Button("Por defecto", ImVec2(hw, 0))) { t.palette.clear(); spriteTools(); savePaletteTo(proj.doc); proj.dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vuelve a la paleta DawnBringer 32");
}

void SpriteEditor::drawTimeline() {
    int frames = (int)st.layers[0].cels.size(), layers = (int)st.layers.size();
    if (ui::iconButton("play", playing ? Icon::Stop : Icon::Play, playing, playing ? "Detener (Enter)" : "Reproducir (Enter)", 26)) { playing = !playing; playT = 0; }
    ImGui::SameLine();
    if (ui::iconButton("prevf", Icon::Prev, false, "Frame anterior (,)", 26)) curFrame = (curFrame + frames - 1) % frames;
    ImGui::SameLine();
    if (ui::iconButton("nextf", Icon::Next, false, "Frame siguiente (.)", 26)) curFrame = (curFrame + 1) % frames;
    ImGui::SameLine(0, 16);
    if (ui::iconButton("addf", Icon::Plus, false, "Nuevo frame vacío", 26)) addFrame(false);
    ImGui::SameLine();
    if (ui::iconButton("dupf", Icon::Copy, false, "Duplicar frame (Alt+D)", 26)) addFrame(true);
    ImGui::SameLine();
    if (ui::iconButton("delf", Icon::Trash, false, "Borrar frame", 26)) deleteFrame();
    ImGui::SameLine();
    if (ui::iconButton("mvfl", Icon::Prev, false, "Mover el frame a la izquierda", 22)) moveFrame(-1);
    ImGui::SameLine(0, 2);
    if (ui::iconButton("mvfr", Icon::Next, false, "Mover el frame a la derecha", 22)) moveFrame(1);
    ImGui::SameLine(0, 16);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Frame %d/%d", curFrame + 1, frames); ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    int dur = st.durations[curFrame];
    if (ImGui::InputInt("ms##dur", &dur, 10)) { st.durations[curFrame] = std::clamp(dur, 10, 10000); dirtyForProject = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duración de este frame en milisegundos");
    ImGui::SameLine(0, 16);
    if (ui::iconButton("addl", Icon::Plus, false, "Nueva capa", 26)) addLayer();
    ImGui::SameLine();
    if (ui::iconButton("dell", Icon::Trash, false, "Borrar capa", 26)) deleteLayer();
    ImGui::SameLine();
    if (ui::iconButton("upl", Icon::Up, false, "Subir capa", 26)) moveLayer(1);
    ImGui::SameLine();
    if (ui::iconButton("dnl", Icon::Down, false, "Bajar capa", 26)) moveLayer(-1);
    ImGui::SameLine();
    if (ui::iconButton("dupl", Icon::Copy, false, "Duplicar capa", 26)) duplicateLayer();
    ImGui::SameLine();
    if (ImGui::SmallButton("Combinar")) mergeDown();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pega la capa actual sobre la de abajo (en todos los frames)");

    ImGuiTableFlags tf = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##tl", frames + 1, tf, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupScrollFreeze(1, 1);
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().MouseWheel != 0 &&
            (ImGui::GetIO().KeyShift || ImGui::GetScrollMaxY() <= 0)) {
            ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 78);
            ImGui::GetIO().MouseWheel = 0;
        }
        static int lastFrame = -1;
        ImGui::TableSetupColumn("Capas", ImGuiTableColumnFlags_WidthFixed, 230);
        for (int f = 0; f < frames; f++) ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 26);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("Capas");
        for (int f = 0; f < frames; f++) {
            ImGui::TableSetColumnIndex(f + 1);
            ImGui::PushID(f);
            char lbl[16]; snprintf(lbl, sizeof lbl, "%d", f + 1);
            if (f == curFrame) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(75, 54, 153, 255));
                if (lastFrame != curFrame) { ImGui::SetScrollHereX(0.5f); lastFrame = curFrame; }
            }
            if (ImGui::Selectable(lbl, f == curFrame, 0, ImVec2(22, 0))) curFrame = f;
            if (ImGui::BeginPopupContextItem("##fctx")) {
                curFrame = f;
                if (ImGui::MenuItem("Nuevo frame después")) addFrame(false);
                if (ImGui::MenuItem("Duplicar frame")) addFrame(true);
                if (ImGui::MenuItem("Borrar frame")) deleteFrame();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        for (int li = layers - 1; li >= 0; li--) {
            SpriteLayer &L = st.layers[li];
            ImGui::TableNextRow(0, 26);
            ImGui::PushID(li);
            ImGui::TableSetColumnIndex(0);
            if (li == curLayer) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(60, 48, 110, 255));
            if (ui::iconButton("vis", L.visible ? Icon::Eye : Icon::EyeOff, false, "Mostrar u ocultar", 22)) { L.visible = !L.visible; dirtyForProject = true; }
            ImGui::SameLine(0, 2);
            if (ui::iconButton("lock", L.locked ? Icon::Lock : Icon::Unlock, L.locked, "Bloquear", 22)) L.locked = !L.locked;
            ImGui::SameLine();
            if (renaming == li) {
                ImGui::SetNextItemWidth(110);
                if (ImGui::IsWindowAppearing() || ImGui::IsItemDeactivated()) {}
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##rn", renameBuf, sizeof renameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    if (renameBuf[0]) { L.name = renameBuf; dirtyForProject = true; }
                    renaming = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) renaming = -1;
            } else {
                if (ImGui::Selectable(L.name.c_str(), li == curLayer, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(108, 0))) {
                    curLayer = li;
                    if (ImGui::IsMouseDoubleClicked(0)) { renaming = li; snprintf(renameBuf, sizeof renameBuf, "%s", L.name.c_str()); }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Doble clic para renombrar");
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(46);
            int op = L.opacity * 100 / 255;
            if (ImGui::DragInt("##op", &op, 1, 0, 100, "%d%%")) { L.opacity = std::clamp(op, 0, 100) * 255 / 100; dirtyForProject = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opacidad de la capa");
            for (int f = 0; f < frames; f++) {
                ImGui::TableSetColumnIndex(f + 1);
                ImGui::PushID(f);
                ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##cel", ImVec2(22, 22))) { curLayer = li; curFrame = f; }
                ImDrawList *dl = ImGui::GetWindowDrawList();
                bool cur = li == curLayer && f == curFrame;
                if (cur) dl->AddRectFilled(p, ImVec2(p.x + 22, p.y + 22), IM_COL32(157, 125, 255, 90), 4);
                ImVec2 cc(p.x + 11, p.y + 11);
                if (celEmpty(li, f)) dl->AddCircle(cc, 4, IM_COL32(140, 140, 155, 255), 0, 1.5f);
                else dl->AddCircleFilled(cc, 5, IM_COL32(220, 220, 232, 255));
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void SpriteEditor::handleShortcuts() {
    ui::noteFocus("sprite", spriteId);
    if (int h = ui::takeHistoryRequest()) { h > 0 ? redo() : undo(); }
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
    auto &t = spriteTools();
    ImGuiIO &io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl;
    if (ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { io.KeyShift ? redo() : undo(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) selectAll();
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copySel(false);
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) copySel(true);
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) paste();
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) invertSelection();
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) dropSelection();
        return;
    }
    if (!tileMode && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_D, false)) { addFrame(true); return; }
    if (!tileMode && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_N, false)) { addFrame(false); return; }
    if (io.KeyShift && (ImGui::IsKeyPressed(ImGuiKey_M, false) || ImGui::IsKeyPressed(ImGuiKey_B, false) || ImGui::IsKeyPressed(ImGuiKey_G, false))) {
        if (floating) commitPreview();
        t.tool = ImGui::IsKeyPressed(ImGuiKey_M, false) ? Tool::SelEllipse : ImGui::IsKeyPressed(ImGuiKey_B, false) ? Tool::Spray : Tool::Gradient;
    } else for (auto &ti : kTools) if (ti.key != ImGuiKey_None && ImGui::IsKeyPressed(ti.key, false)) { if (floating) commitPreview(); t.tool = ti.t; }
    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) std::swap(t.fg, t.bg);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) t.brush = std::max(1, t.brush - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) t.brush = std::min(t.tool == Tool::Spray ? 32 : 16, t.brush + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) clearSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { if (floating) commitPreview(); dropSelection(); }
    int frames = (int)st.layers[0].cels.size();
    if (tileMode) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Comma)) curFrame = (curFrame + frames - 1) % frames;
    if (ImGui::IsKeyPressed(ImGuiKey_Period)) curFrame = (curFrame + 1) % frames;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) { playing = !playing; playT = 0; }
}

void SpriteEditor::draw(Project &proj) {
    std::string title = "Sprite: " + name + "###spr_" + spriteId;
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(1000, 680), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
    drawBody(proj);
    ImGui::End();
}

void SpriteEditor::drawBody(Project &proj) {
    handleShortcuts();
    drawOptions();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    static size_t lastLayers = 0;
    float want = std::min(300.0f, 70.0f + 28.0f * (float)st.layers.size() + 30.0f);
    if (tlH < 0 || (st.layers.size() > lastLayers && tlH < want)) tlH = want;
    lastLayers = st.layers.size();
    tlH = std::clamp(tlH, 90.0f, std::max(90.0f, avail.y * 0.75f));
    float timelineH = tileMode ? 0.0f : tlH + 6;
    float sideW = 250;
    ImGui::BeginChild("##tools", ImVec2(44, avail.y - timelineH), ImGuiChildFlags_None);
    drawToolbar();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##canvas", ImVec2(avail.x - 44 - sideW - 16, avail.y - timelineH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##side", ImVec2(sideW, avail.y - timelineH), ImGuiChildFlags_None);
    drawSidePanel(proj);
    ImGui::EndChild();
    if (!tileMode) {
        ImVec2 sp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##tlsplit", ImVec2(avail.x, 6));
        bool hv = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) tlH -= ImGui::GetIO().MouseDelta.y;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(sp.x + avail.x / 2 - 24, sp.y + 2), ImVec2(sp.x + avail.x / 2 + 24, sp.y + 4), hv ? ui::kAccent : ui::kLine, 2);
        ImGui::BeginChild("##timeline", ImVec2(0, 0), ImGuiChildFlags_Borders);
        drawTimeline();
        ImGui::EndChild();
    }
    g_palDlg.draw();
}
