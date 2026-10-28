// Source 2D - editor de sprites: seleccion por pixeles y herramientas de corte, paletas en archivos (compatibles con
// Aseprite), abrir imagenes y exportar (imagenes, hojas de sprites con su JSON, GIF animado y .s2dsprite).
#include "sprite_editor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "raylib.h"
#include "external/msf_gif.h"     // el codificador de GIF que raylib ya trae (implementado en rcore)

namespace fs = std::filesystem;

const char *kPaletteOpenExt = ".gpl|.pal|.hex|.txt|.act|.ase|.aseprite|.png|.gif|.bmp|.jpg|.jpeg|.tga|.qoi";
const char *kPaletteSaveExt = ".gpl|.pal|.hex|.txt|.act|.png";

static std::string lowerExt(const std::string &path) {
    std::string e = fs::path(path).extension().string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e;
}

/* ================= seleccion ================= */
void SpriteEditor::setSelection(const std::vector<uint8_t> &m, int mode) {
    int n = st.w * st.h;
    bool had = selPrev.size() == (size_t)n;
    std::vector<uint8_t> out(n, 0);
    for (int i = 0; i < n; i++) {
        bool a = had && selPrev[i], b = i < (int)m.size() && m[i];
        out[i] = mode == 1 ? (a || b) : mode == 2 ? (a && !b) : b;
    }
    int x0 = st.w, y0 = st.h, x1 = -1, y1 = -1;
    for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++)
        if (out[y * st.w + x]) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    if (x1 < 0) { dropSelection(); return; }
    selMask = std::move(out);
    sel = SelRect{x0, y0, x1 - x0 + 1, y1 - y0 + 1, true};
}

void SpriteEditor::selectRect(int x, int y, int w, int h, int mode) {
    int n = st.w * st.h;
    selPrev = sel.active && selMask.size() == (size_t)n ? selMask : std::vector<uint8_t>();
    std::vector<uint8_t> m(n, 0);
    for (int yy = std::max(0, y); yy < std::min(st.h, y + h); yy++)
        for (int xx = std::max(0, x); xx < std::min(st.w, x + w); xx++) m[yy * st.w + xx] = 1;
    setSelection(m, mode);
}

// varita magica: los pixeles de la capa actual parecidos al del punto (contiguos o en todo el lienzo)
void SpriteEditor::selectByColor(int x, int y, bool contiguous, int mode) {
    if (x < 0 || y < 0 || x >= st.w || y >= st.h) return;
    int n = st.w * st.h, tol = spriteTools().wandTolerance;
    const std::vector<Px> &d = cel();
    Px t = d[y * st.w + x];
    auto like = [&](const Px &p) {
        if (p.a == 0 || t.a == 0) return p.a == 0 && t.a == 0;
        return std::abs(p.r - t.r) <= tol && std::abs(p.g - t.g) <= tol && std::abs(p.b - t.b) <= tol && std::abs(p.a - t.a) <= tol;
    };
    selPrev = sel.active && selMask.size() == (size_t)n ? selMask : std::vector<uint8_t>();
    std::vector<uint8_t> m(n, 0);
    if (!contiguous) {
        for (int i = 0; i < n; i++) m[i] = like(d[i]);
    } else {
        std::vector<int> stack{y * st.w + x};
        while (!stack.empty()) {
            int i = stack.back(); stack.pop_back();
            if (m[i] || !like(d[i])) continue;
            m[i] = 1;
            int px = i % st.w, py = i / st.w;
            if (px > 0) stack.push_back(i - 1);
            if (px < st.w - 1) stack.push_back(i + 1);
            if (py > 0) stack.push_back(i - st.w);
            if (py < st.h - 1) stack.push_back(i + st.w);
        }
    }
    setSelection(m, mode);
}

void SpriteEditor::invertSelection() {
    int n = st.w * st.h;
    std::vector<uint8_t> m(n, 1);
    if (sel.active && selMask.size() == (size_t)n) for (int i = 0; i < n; i++) m[i] = !selMask[i];
    selPrev.clear();
    setSelection(m, 0);
}

/* ================= recortes ================= */
void SpriteEditor::cropTo(int x, int y, int w, int h) {
    for (auto &L : st.layers) for (auto &c : L.cels) {
        std::vector<Px> nc(w * h);
        for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) {
            int sx2 = x + xx, sy2 = y + yy;
            if (sx2 >= 0 && sy2 >= 0 && sx2 < st.w && sy2 < st.h) nc[yy * w + xx] = c[sy2 * st.w + sx2];
        }
        c = std::move(nc);
    }
    st.w = w; st.h = h; newW = w; newH = h;
    dropSelection(); fitPending = true; dirtyForProject = true;
}

void SpriteEditor::cropToSelection() {
    if (!sel.active) { status = "Primero selecciona la zona que quieres conservar"; return; }
    pushUndo();
    cropTo(sel.x, sel.y, sel.w, sel.h);
    status = "Lienzo recortado a " + std::to_string(st.w) + "x" + std::to_string(st.h);
}

void SpriteEditor::trimCanvas() {
    int x0 = st.w, y0 = st.h, x1 = -1, y1 = -1;
    for (auto &L : st.layers) for (auto &c : L.cels)
        for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++)
            if (c[y * st.w + x].a) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    if (x1 < 0) { status = "El sprite está vacío"; return; }
    if (x0 == 0 && y0 == 0 && x1 == st.w - 1 && y1 == st.h - 1) { status = "No hay bordes vacíos que quitar"; return; }
    pushUndo();
    cropTo(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    status = "Bordes vacíos quitados: ahora " + std::to_string(st.w) + "x" + std::to_string(st.h);
}

void SpriteEditor::cutToNewLayer() {
    if (!sel.active) { status = "Primero selecciona lo que quieres pasar a otra capa"; return; }
    if (!canPaint()) return;
    pushUndo();
    int n = st.w * st.h;
    SpriteLayer L;
    L.name = st.layers[curLayer].name + " (corte)";
    L.cels.assign(st.layers[0].cels.size(), std::vector<Px>(n));
    std::vector<Px> &src = cel();
    for (int i = 0; i < n; i++) if (selHas(i) && src[i].a) { L.cels[curFrame][i] = src[i]; src[i] = Px{}; }
    st.layers.insert(st.layers.begin() + curLayer + 1, std::move(L));
    curLayer++;
    dirtyForProject = true;
    status = "Selección cortada a una capa nueva";
}

// una imagen con varios dibujos (una hoja) pasa a ser un frame por celda, de izquierda a derecha y de arriba abajo
void SpriteEditor::sliceToFrames(int cw, int ch, bool skipEmpty) {
    cw = std::clamp(cw, 1, st.w); ch = std::clamp(ch, 1, st.h);
    int cols = (st.w + cw - 1) / cw, rows = (st.h + ch - 1) / ch;
    if (cols * rows <= 1) { status = "La celda es tan grande como el lienzo: no hay nada que cortar"; return; }
    std::vector<std::pair<int, int>> cells;
    for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) {
        bool any = !skipEmpty;
        for (auto &L : st.layers) {
            const std::vector<Px> &src = L.cels[curFrame];
            for (int y = r * ch; y < std::min(st.h, (r + 1) * ch) && !any; y++)
                for (int x = c * cw; x < std::min(st.w, (c + 1) * cw) && !any; x++) any = src[y * st.w + x].a != 0;
        }
        if (any) cells.push_back({c * cw, r * ch});
    }
    if (cells.empty()) { status = "Todas las celdas están vacías"; return; }
    pushUndo();
    int dur = st.durations[curFrame];
    for (auto &L : st.layers) {
        std::vector<Px> src = L.cels[curFrame];
        L.cels.clear();
        for (auto &cc : cells) {
            std::vector<Px> nc(cw * ch);
            for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) {
                int sx2 = cc.first + x, sy2 = cc.second + y;
                if (sx2 < st.w && sy2 < st.h) nc[y * cw + x] = src[sy2 * st.w + sx2];
            }
            L.cels.push_back(std::move(nc));
        }
    }
    st.durations.assign(cells.size(), dur);
    st.w = cw; st.h = ch; newW = cw; newH = ch; curFrame = 0;
    dropSelection(); fitPending = true; dirtyForProject = true;
    status = "Cortado en " + std::to_string(cells.size()) + " frames de " + std::to_string(cw) + "x" + std::to_string(ch);
}

/* ================= hojas y exportar ================= */
std::vector<Px> SpriteEditor::sheet(int layout, int &W, int &H, std::vector<SelRect> *cells) const {
    int F = (int)st.layers[0].cels.size();
    std::vector<int> frames;
    if (layout == 0) frames.push_back(curFrame);
    else for (int f = 0; f < F; f++) frames.push_back(f);
    int n = (int)frames.size(), cols = layout == 1 ? n : layout == 2 ? 1 : layout == 3 ? (int)std::ceil(std::sqrt((double)n)) : 1;
    int rows = (n + cols - 1) / cols;
    W = st.w * cols; H = st.h * rows;
    std::vector<Px> out(W * H), comp;
    for (int k = 0; k < n; k++) {
        composite(frames[k], comp, false);
        int ox = (k % cols) * st.w, oy = (k / cols) * st.h;
        for (int y = 0; y < st.h; y++) for (int x = 0; x < st.w; x++) out[(oy + y) * W + ox + x] = comp[y * st.w + x];
        if (cells) cells->push_back(SelRect{ox, oy, st.w, st.h, true});
    }
    return out;
}

static bool writeImage(const std::string &path, std::vector<Px> &px, int w, int h) {
    Image im{px.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    return ExportImage(im, path.c_str());
}

bool SpriteEditor::exportFile(const std::string &path, int layout, bool withJson, std::string &err) const {
    std::string ext = lowerExt(path);
    int F = (int)st.layers[0].cels.size();
    if (ext == ".s2dsprite") {                       // formato propio: capas, frames, duraciones y paleta (se puede seguir editando)
        json j;
        j["format"] = "source2d-sprite";
        writeTo(j);
        savePaletteTo(j);
        std::ofstream o(path, std::ios::binary);
        if (!(o << j.dump())) { err = "No se pudo escribir " + path; return false; }
        return true;
    }
    if (ext == ".gif") {                             // GIF animado con las duraciones de cada frame y transparencia
        MsfGifState g{};
        int oldThr = msf_gif_alpha_threshold;
        msf_gif_alpha_threshold = 128;
        if (!msf_gif_begin(&g, st.w, st.h)) { msf_gif_alpha_threshold = oldThr; err = "No se pudo crear el GIF"; return false; }
        std::vector<Px> comp;
        int first = layout == 0 ? curFrame : 0, last = layout == 0 ? curFrame : F - 1;
        for (int f = first; f <= last; f++) {
            composite(f, comp, false);
            msf_gif_frame(&g, (uint8_t *)comp.data(), std::max(2, st.durations[f] / 10), 16, st.w * 4);
        }
        MsfGifResult r = msf_gif_end(&g);
        msf_gif_alpha_threshold = oldThr;
        bool ok = false;
        if (r.data) {
            std::ofstream o(path, std::ios::binary);
            ok = (bool)o.write((const char *)r.data, (std::streamsize)r.dataSize);
        }
        msf_gif_free(r);
        if (!ok) err = "No se pudo escribir " + path;
        return ok;
    }
    static const char *kImg[] = {".png", ".bmp", ".tga", ".qoi", ".jpg", ".jpeg"};
    if (std::find_if(std::begin(kImg), std::end(kImg), [&](const char *e) { return ext == e; }) == std::end(kImg)) {
        err = "Formato no admitido: " + ext + " (usa .png, .gif, .bmp, .tga, .qoi, .jpg o .s2dsprite)";
        return false;
    }
    if (layout == 4) {                               // un archivo por frame: nombre_1.png, nombre_2.png...
        fs::path p(path);
        std::vector<Px> comp;
        for (int f = 0; f < F; f++) {
            composite(f, comp, false);
            std::string fp = (p.parent_path() / (p.stem().string() + "_" + std::to_string(f + 1) + ext)).string();
            if (!writeImage(fp, comp, st.w, st.h)) { err = "No se pudo escribir " + fp; return false; }
        }
        return true;
    }
    int W = 0, H = 0;
    std::vector<SelRect> cells;
    std::vector<Px> img = sheet(layout, W, H, &cells);
    if (!writeImage(path, img, W, H)) { err = "No se pudo escribir " + path; return false; }
    if (withJson && layout != 0) {                   // el JSON de la hoja en el formato de Aseprite (hash)
        json fr = json::object();
        for (size_t k = 0; k < cells.size(); k++) {
            const SelRect &c = cells[k];
            fr[name + " " + std::to_string(k) + ".aseprite"] = {
                {"frame", {{"x", c.x}, {"y", c.y}, {"w", c.w}, {"h", c.h}}}, {"rotated", false}, {"trimmed", false},
                {"spriteSourceSize", {{"x", 0}, {"y", 0}, {"w", c.w}, {"h", c.h}}}, {"sourceSize", {{"w", c.w}, {"h", c.h}}},
                {"duration", st.durations[k]}};
        }
        json meta = {{"app", "Source 2D"}, {"version", "1"}, {"image", fs::path(path).filename().string()}, {"format", "RGBA8888"},
                     {"size", {{"w", W}, {"h", H}}}, {"scale", "1"},
                     {"frameTags", json::array({{{"name", name}, {"from", 0}, {"to", (int)cells.size() - 1}, {"direction", "forward"}}})}};
        json j = {{"frames", fr}, {"meta", meta}};
        fs::path jp = fs::path(path).replace_extension(".json");
        std::ofstream o(jp, std::ios::binary);
        if (!(o << j.dump(1))) { err = "No se pudo escribir " + jp.string(); return false; }
    }
    return true;
}

bool SpriteEditor::loadImageFile(const std::string &path, std::string &err) {
    std::string ext = lowerExt(path);
    if (ext == ".s2dsprite") {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        json j = json::parse(ss.str(), nullptr, false);
        if (j.is_discarded() || !j.is_object()) { err = "No es un sprite de Source 2D: " + path; return false; }
        std::string keepId = spriteId;
        load(j);
        spriteId = keepId;
        loadPaletteFrom(j);
    } else {
        int frames = 1;
        Image im = ext == ".gif" ? LoadImageAnim(path.c_str(), &frames) : LoadImage(path.c_str());
        if (!im.data) { err = "No se pudo abrir la imagen " + path; return false; }
        ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        if (im.width > 2048 || im.height > 2048) { UnloadImage(im); err = "La imagen es demasiado grande (máximo 2048 x 2048)"; return false; }
        frames = std::max(1, frames);
        st = SpriteState{};
        st.w = im.width; st.h = im.height;
        SpriteLayer L;
        const Px *src = (const Px *)im.data;
        for (int f = 0; f < frames; f++) L.cels.emplace_back(src + (size_t)f * st.w * st.h, src + (size_t)(f + 1) * st.w * st.h);
        st.layers.push_back(std::move(L));
        st.durations.assign(frames, 100);
        UnloadImage(im);
        name = fs::path(path).stem().string();
        curLayer = 0; curFrame = 0; newW = st.w; newH = st.h;
    }
    undoStack.clear(); redoStack.clear();
    dropSelection(); fitPending = true; dirtyForProject = true;
    return true;
}

/* ================= paletas en archivos ================= */
static Px rgb(int r, int g, int b) { return Px{(uint8_t)std::clamp(r, 0, 255), (uint8_t)std::clamp(g, 0, 255), (uint8_t)std::clamp(b, 0, 255), 255}; }

static void addUnique(std::vector<Px> &out, Px c) {
    if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
}

static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t be16(const unsigned char *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static float beF(const unsigned char *p) { uint32_t u = be32(p); float f; memcpy(&f, &u, 4); return f; }

// paleta de un archivo de Aseprite (.ase/.aseprite): chunk de paleta del primer frame
static bool asepritePalette(const std::vector<unsigned char> &d, std::vector<Px> &out) {
    if (d.size() < 128 || le16(&d[4]) != 0xA5E0) return false;
    size_t pos = 128;
    if (pos + 16 > d.size()) return false;
    uint32_t fbytes = le32(&d[pos]);
    if (le16(&d[pos + 4]) != 0xF1FA) return false;
    uint32_t nch = le32(&d[pos + 12]);
    if (!nch) nch = le16(&d[pos + 6]);
    size_t end = std::min(d.size(), pos + fbytes), c = pos + 16;
    std::vector<Px> oldPal;
    for (uint32_t k = 0; k < nch && c + 6 <= end; k++) {
        uint32_t sz = le32(&d[c]);
        uint16_t type = le16(&d[c + 4]);
        if (sz < 6 || c + sz > end) break;
        const unsigned char *q = &d[c + 6];
        if (type == 0x2019 && sz >= 26) {            // paleta nueva: RGBA y nombre opcional
            uint32_t first = le32(q + 4), last = le32(q + 8);
            size_t e = 20;
            out.clear();
            for (uint32_t i = first; i <= last && c + 6 + e + 6 <= c + sz; i++) {
                uint16_t flags = le16(q + e);
                Px px{q[e + 2], q[e + 3], q[e + 4], q[e + 5]};
                e += 6;
                if (flags & 1) e += 2 + le16(q + e);
                out.push_back(px);
            }
            return !out.empty();
        }
        if ((type == 0x0004 || type == 0x0011) && oldPal.empty()) {   // paletas antiguas (0-255 o 0-63)
            uint16_t packets = le16(q);
            size_t e = 2;
            size_t idx = 0;                                    // cada paquete: cuantos colores saltar y cuantos vienen
            for (uint16_t pk = 0; pk < packets && c + 6 + e + 2 <= c + sz; pk++) {
                idx += q[e];
                int n = q[e + 1] ? q[e + 1] : 256;
                e += 2;
                for (int i = 0; i < n && c + 6 + e + 3 <= c + sz; i++, e += 3, idx++) {
                    int m = type == 0x0011 ? 4 : 1;
                    if (oldPal.size() <= idx) oldPal.resize(idx + 1, rgb(0, 0, 0));
                    oldPal[idx] = rgb(q[e] * m, q[e + 1] * m, q[e + 2] * m);
                }
            }
        }
        c += sz;
    }
    if (!oldPal.empty()) { out = oldPal; return true; }
    return false;
}

// Adobe Swatch Exchange (.ase que empieza por "ASEF")
static bool swatchExchange(const std::vector<unsigned char> &d, std::vector<Px> &out) {
    if (d.size() < 12 || memcmp(d.data(), "ASEF", 4) != 0) return false;
    uint32_t blocks = be32(&d[8]);
    size_t p = 12;
    for (uint32_t b = 0; b < blocks && p + 6 <= d.size(); b++) {
        uint16_t type = be16(&d[p]);
        uint32_t len = be32(&d[p + 2]);
        size_t q = p + 6;
        p = q + len;
        if (type != 0x0001 || p > d.size() || len < 2) continue;
        size_t nm = be16(&d[q]);
        q += 2 + nm * 2;
        if (q + 4 > p) continue;
        std::string model((const char *)&d[q], 4);
        q += 4;
        if (model == "RGB " && q + 12 <= p) out.push_back(rgb((int)std::lround(beF(&d[q]) * 255), (int)std::lround(beF(&d[q + 4]) * 255), (int)std::lround(beF(&d[q + 8]) * 255)));
        else if (model == "Gray" && q + 4 <= p) { int v = (int)std::lround(beF(&d[q]) * 255); out.push_back(rgb(v, v, v)); }
        else if (model == "CMYK" && q + 16 <= p) {
            float c = beF(&d[q]), m = beF(&d[q + 4]), y = beF(&d[q + 8]), k = beF(&d[q + 12]);
            out.push_back(rgb((int)std::lround(255 * (1 - c) * (1 - k)), (int)std::lround(255 * (1 - m) * (1 - k)), (int)std::lround(255 * (1 - y) * (1 - k))));
        }
    }
    return !out.empty();
}

bool loadPaletteFile(const std::string &path, std::vector<Px> &out, std::string &err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "No se pudo leer " + path; return false; }
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string ext = lowerExt(path), text(d.begin(), d.end());
    out.clear();
    if (ext == ".ase" || ext == ".aseprite") {
        if (!asepritePalette(d, out) && !swatchExchange(d, out)) { err = "No se encontró una paleta en " + path; return false; }
    } else if (ext == ".act") {
        int n = 256;
        if (d.size() >= 772) { n = be16(&d[768]); if (n <= 0 || n > 256) n = 256; }
        for (int i = 0; i < n && (size_t)i * 3 + 2 < d.size(); i++) out.push_back(rgb(d[i * 3], d[i * 3 + 1], d[i * 3 + 2]));
    } else if (ext == ".pal" && d.size() >= 24 && memcmp(d.data(), "RIFF", 4) == 0 && memcmp(&d[8], "PAL ", 4) == 0) {
        int n = le16(&d[22]);                        // RIFF PAL de Microsoft
        for (int i = 0; i < n && 24 + (size_t)i * 4 + 3 <= d.size(); i++) out.push_back(rgb(d[24 + i * 4], d[25 + i * 4], d[26 + i * 4]));
    } else if (ext == ".gpl" || ext == ".pal" || ext == ".hex" || ext == ".txt") {
        std::istringstream ss(text);
        std::string line;
        int lineNo = 0;
        bool jasc = text.rfind("JASC-PAL", 0) == 0;
        while (std::getline(ss, line)) {
            lineNo++;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t a = line.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            line = line.substr(a);
            if (ext == ".gpl") {
                if (line[0] == '#' || line.rfind("GIMP", 0) == 0 || line.rfind("Name:", 0) == 0 || line.rfind("Columns:", 0) == 0) continue;
                int r, g, b;
                if (sscanf(line.c_str(), "%d %d %d", &r, &g, &b) == 3) out.push_back(rgb(r, g, b));
            } else if (jasc) {
                int r, g, b;
                if (lineNo > 3 && sscanf(line.c_str(), "%d %d %d", &r, &g, &b) == 3) out.push_back(rgb(r, g, b));
            } else {                                 // .hex de Lospec ("rrggbb") o .txt de Paint.NET ("aarrggbb", ';' comenta)
                if (line[0] == ';') continue;
                if (line[0] == '#') line = line.substr(1);
                unsigned v = 0;
                size_t len = line.find_first_not_of("0123456789abcdefABCDEF");
                len = len == std::string::npos ? line.size() : len;
                if (len != 6 && len != 8) continue;
                v = (unsigned)std::stoul(line.substr(0, len), nullptr, 16);
                Px c = rgb((v >> 16) & 255, (v >> 8) & 255, v & 255);
                if (len == 8) c.a = (uint8_t)(v >> 24);
                if (c.a) out.push_back(c);
            }
        }
    } else {                                         // imagen: sus colores en orden de aparicion (como "cargar paleta" de Aseprite)
        Image im = LoadImage(path.c_str());
        if (!im.data) { err = "No se pudo abrir " + path; return false; }
        ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        const Px *px = (const Px *)im.data;
        for (int i = 0; i < im.width * im.height && out.size() < 256; i++) if (px[i].a) addUnique(out, Px{px[i].r, px[i].g, px[i].b, 255});
        UnloadImage(im);
    }
    if (out.empty()) { err = "El archivo no tiene colores: " + path; return false; }
    if (out.size() > 256) out.resize(256);
    return true;
}

bool savePaletteFile(const std::string &path, const std::vector<Px> &pal, std::string &err) {
    std::string ext = lowerExt(path);
    std::ofstream o(path, std::ios::binary);
    if (!o) { err = "No se pudo escribir " + path; return false; }
    char b[64];
    if (ext == ".gpl") {
        o << "GIMP Palette\nName: " << fs::path(path).stem().string() << "\nColumns: 8\n#\n";
        for (auto &c : pal) { snprintf(b, sizeof b, "%3d %3d %3d\t#%02x%02x%02x\n", c.r, c.g, c.b, c.r, c.g, c.b); o << b; }
    } else if (ext == ".pal") {
        o << "JASC-PAL\r\n0100\r\n" << pal.size() << "\r\n";
        for (auto &c : pal) o << (int)c.r << " " << (int)c.g << " " << (int)c.b << "\r\n";
    } else if (ext == ".hex") {
        for (auto &c : pal) { snprintf(b, sizeof b, "%02x%02x%02x\n", c.r, c.g, c.b); o << b; }
    } else if (ext == ".txt") {
        o << ";paint.net Palette File\n;Colors: " << pal.size() << "\n";
        for (auto &c : pal) { snprintf(b, sizeof b, "%02X%02X%02X%02X\n", c.a ? c.a : 255, c.r, c.g, c.b); o << b; }
    } else if (ext == ".act") {
        unsigned char buf[772] = {0};
        int n = (int)std::min<size_t>(pal.size(), 256);
        for (int i = 0; i < n; i++) { buf[i * 3] = pal[i].r; buf[i * 3 + 1] = pal[i].g; buf[i * 3 + 2] = pal[i].b; }
        buf[768] = (unsigned char)(n >> 8); buf[769] = (unsigned char)n; buf[770] = 0xFF; buf[771] = 0xFF;
        o.write((const char *)buf, sizeof buf);
    } else if (ext == ".png") {
        o.close();
        std::vector<Px> px(pal);
        for (auto &c : px) c.a = 255;
        if (px.empty()) px.push_back(Px{0, 0, 0, 255});
        Image im{px.data(), (int)px.size(), 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        if (!ExportImage(im, path.c_str())) { err = "No se pudo escribir " + path; return false; }
        return true;
    } else { err = "Formato de paleta no admitido: " + ext; return false; }
    if (!o) { err = "No se pudo escribir " + path; return false; }
    return true;
}
