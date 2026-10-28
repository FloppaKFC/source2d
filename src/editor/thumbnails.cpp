// Source 2D - miniaturas de proyectos (ver thumbnails.hpp)
#include "thumbnails.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include "assets.hpp"
#include "fonts.hpp"
#include "raylib.h"

namespace fs = std::filesystem;

namespace {

struct Canvas {
    std::vector<Px> &px;
    int w, h;
    float s, ox, oy;   // escala y desplazamiento de mundo a pixel
    void blend(int x, int y, Px c, int alpha) {
        int a = c.a * alpha / 255;
        if (a <= 0) return;
        Px &d = px[y * w + x];
        d.r = (uint8_t)((c.r * a + d.r * (255 - a)) / 255);
        d.g = (uint8_t)((c.g * a + d.g * (255 - a)) / 255);
        d.b = (uint8_t)((c.b * a + d.b * (255 - a)) / 255);
        d.a = 255;
    }
};

// imagen (sw x sh) centrada en (cx, cy) del mundo, con su tamano base bw x bh, escala, giro y opacidad (0-255)
void drawImage(Canvas &cv, const std::vector<Px> &src, int sw, int sh, float cx, float cy, float bw, float bh, float sx, float sy, float angDeg, int alpha) {
    if (src.empty() || sw <= 0 || sh <= 0 || sx == 0 || sy == 0) return;
    float a = angDeg * 3.14159265f / 180, cs = std::cos(a), sn = std::sin(a);
    float hw = std::fabs(bw * sx) / 2, hh = std::fabs(bh * sy) / 2, r = std::sqrt(hw * hw + hh * hh);
    int x0 = std::max(0, (int)std::floor(cv.ox + (cx - r) * cv.s)), x1 = std::min(cv.w - 1, (int)std::ceil(cv.ox + (cx + r) * cv.s));
    int y0 = std::max(0, (int)std::floor(cv.oy + (cy - r) * cv.s)), y1 = std::min(cv.h - 1, (int)std::ceil(cv.oy + (cy + r) * cv.s));
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float wx = (x + 0.5f - cv.ox) / cv.s - cx, wy = (y + 0.5f - cv.oy) / cv.s - cy;
        float lx = wx * cs + wy * sn, ly = -wx * sn + wy * cs;          // al espacio local del objeto
        float u = lx / (bw * sx) + 0.5f, v = ly / (bh * sy) + 0.5f;      // la escala negativa voltea
        if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
        cv.blend(x, y, src[(int)(v * sh) * sw + (int)(u * sw)], alpha);
    }
}

void fillRect(Canvas &cv, float x0w, float y0w, float x1w, float y1w, Px c, int alpha) {
    int x0 = std::max(0, (int)std::lround(cv.ox + x0w * cv.s)), x1 = std::min(cv.w, (int)std::lround(cv.ox + x1w * cv.s));
    int y0 = std::max(0, (int)std::lround(cv.oy + y0w * cv.s)), y1 = std::min(cv.h, (int)std::lround(cv.oy + y1w * cv.s));
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) cv.blend(x, y, c, alpha);
}

// la fuente del jugador, para que los textos se vean como en el juego
Font &thumbFont() {
    static Font f{};
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        int n = 0;
        const unsigned char *ttf = ui::gameFontTTF(&n);
        f = LoadFontFromMemory(".ttf", ttf, n, 48, nullptr, 0);
    }
    return f;
}

// "{g:score}" y similares se ven como en el primer fotograma: con el valor inicial de la variable o 0
std::string resolveText(const json &doc, std::string s) {
    size_t i;
    while ((i = s.find('{')) != std::string::npos) {
        size_t j = s.find('}', i);
        if (j == std::string::npos) break;
        std::string name = s.substr(i + 1, j - i - 1), val = "0";
        if (name.rfind("g:", 0) == 0) name = name.substr(2);
        if (doc.contains("globals") && doc["globals"].is_array())
            for (auto &g : doc["globals"]) if (jstr(g, "name") == name && g.contains("value")) val = g["value"].is_string() ? g["value"].get<std::string>() : g["value"].dump();
        s.replace(i, j - i + 1, val);
    }
    return s;
}

// texto con una fuente del proyecto: se rasteriza y se copia escalado (vecino mas cercano)
void drawFontText(Canvas &cv, const json &doc, const std::string &font, const std::string &txt, float x, float y, float size, Px c, int alpha, bool bold, const char *align) {
    TextImage img;
    if (!renderTextImage(doc, font, txt, size, c, bold, img)) return;
    float k = img.scale * cv.s, w = img.w * k, h = img.h * k;
    float px = cv.ox + x * cv.s, py = cv.oy + y * cv.s - h / 2;
    if (!strcmp(align, "center")) px -= w / 2; else if (!strcmp(align, "right")) px -= w;
    for (int yy = std::max(0, (int)py); yy < std::min(cv.h, (int)std::ceil(py + h)); yy++)
        for (int xx = std::max(0, (int)px); xx < std::min(cv.w, (int)std::ceil(px + w)); xx++) {
            int sx = (int)((xx + 0.5f - px) / k), sy = (int)((yy + 0.5f - py) / k);
            if (sx < 0 || sy < 0 || sx >= img.w || sy >= img.h) continue;
            cv.blend(xx, yy, img.px[(size_t)sy * img.w + sx], alpha);
        }
}

void drawText(Canvas &cv, const std::string &txt, float x, float y, float size, Px c, int alpha, const char *align, bool vcenter) {
    Font &f = thumbFont();
    float fs = size * cv.s;
    if (!f.glyphCount || fs < 2) return;
    Vector2 m = MeasureTextEx(f, txt.c_str(), fs, 0);
    float px = cv.ox + x * cv.s, py = cv.oy + y * cv.s;
    if (!strcmp(align, "center")) px -= m.x / 2; else if (!strcmp(align, "right")) px -= m.x;
    if (vcenter) py -= m.y / 2;
    Image img{cv.px.data(), cv.w, cv.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    ImageDrawTextEx(&img, f, txt.c_str(), Vector2{px, py}, fs, 0, Color{c.r, c.g, c.b, (unsigned char)(c.a * alpha / 255)});
}

std::vector<Px> parsePixels(const json &arr, int n) {
    std::vector<Px> px(std::max(0, n));
    if (arr.is_array())
        for (int i = 0; i < n && i < (int)arr.size(); i++) if (arr[i].is_string()) px[i] = parseColor(arr[i].get<std::string>());
    return px;
}

const json *byId(const json &doc, const char *list, const std::string &id) {
    if (!doc.contains(list) || !doc[list].is_array()) return nullptr;
    for (auto &x : doc[list]) if (jstr(x, "id") == id) return &x;
    return nullptr;
}

}  // namespace

void renderProjectThumb(const json &doc, int w, int h, std::vector<Px> &out) {
    out.assign(w * h, Px{0x1D, 0x1D, 0x26, 255});
    const json st = doc.contains("settings") ? doc["settings"] : json::object();
    float VW = std::max(1.0, jnum(st, "width", 960)), VH = std::max(1.0, jnum(st, "height", 540));
    float s = std::min(w / VW, h / VH);
    if (!doc.contains("scenes") || !doc["scenes"].is_array() || doc["scenes"].empty()) return;
    const json &sc = doc["scenes"][0];
    // el mundo se ve con el zoom inicial de la camara; los objetos HUD van en pixeles de pantalla
    float cz = sc.contains("camera") && sc["camera"].is_object() ? (float)jnum(sc["camera"], "zoom", 1) : 1;
    if (!(cz > 0)) cz = 1;
    Canvas hud{out, w, h, s, (w - VW * s) / 2, (h - VH * s) / 2};
    Canvas cv{out, w, h, s * cz, hud.ox, hud.oy};
    fillRect(hud, 0, 0, VW, VH, parseColor(jstr(sc, "background", "#202329")), 255);
    VW /= cz; VH /= cz;

    // mapas de tiles
    std::map<std::string, std::vector<Px>> cache;
    if (sc.contains("tilemaps") && sc["tilemaps"].is_array())
        for (auto &tm : sc["tilemaps"]) {
            const json *ts = byId(doc, "tilesets", jstr(tm, "tilesetId"));
            if (!ts || !tm.contains("data") || !tm["data"].is_array() || !ts->contains("tiles")) continue;
            int T = (int)jnum(*ts, "tileSize", 16), cols = (int)jnum(tm, "cols", 0), rows = (int)jnum(tm, "rows", 0);
            float ox = (float)jnum(tm, "x", 0), oy = (float)jnum(tm, "y", 0);
            const json &data = tm["data"], &tiles = (*ts)["tiles"];
            int c0 = std::max(0, (int)std::floor(-ox / T)), c1 = std::min(cols - 1, (int)std::floor((VW - ox) / T));
            int r0 = std::max(0, (int)std::floor(-oy / T)), r1 = std::min(rows - 1, (int)std::floor((VH - oy) / T));
            for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) {
                int idx = r * cols + c;
                if (idx >= (int)data.size() || !data[idx].is_number()) continue;
                int t = data[idx].get<int>();
                if (t < 0 || t >= (int)tiles.size()) continue;
                std::string key = "t:" + jstr(*ts, "id") + ":" + std::to_string(t);
                auto it = cache.find(key);
                if (it == cache.end()) it = cache.emplace(key, parsePixels(tiles[t].contains("pixels") ? tiles[t]["pixels"] : json(), T * T)).first;
                drawImage(cv, it->second, T, T, ox + (c + 0.5f) * T, oy + (r + 0.5f) * T, T, T, 1, 1, 0, 255);
            }
        }

    // instancias en el orden del juego: capa (z), z de la instancia y orden
    if (!sc.contains("instances") || !sc["instances"].is_array()) return;
    const json &insts = sc["instances"];
    auto layerOf = [&](const json &obj) -> const json * { return byId(doc, "layers", jstr(obj, "capa", "base")); };
    std::vector<std::pair<double, int>> order;
    for (int i = 0; i < (int)insts.size(); i++) {
        const json *obj = byId(doc, "objects", jstr(insts[i], "objectId"));
        const json *L = obj ? layerOf(*obj) : nullptr;
        order.push_back({(L ? jnum(*L, "z", 0) : 0) * 1e6 + jnum(insts[i], "z", 0) * 1e3 + i * 1e-3, i});
    }
    std::sort(order.begin(), order.end());
    Canvas &cv0 = cv;
    for (auto &oi : order) {
        const json &it = insts[oi.second];
        const json *obj = byId(doc, "objects", jstr(it, "objectId"));
        if (!obj) continue;
        if (const json *L = layerOf(*obj); L && jbool(*L, "hidden", false)) continue;
        float x = (float)jnum(it, "x", 0), y = (float)jnum(it, "y", 0), sx = (float)jnum(it, "scaleX", 1), sy = (float)jnum(it, "scaleY", 1);
        int alpha = (int)(std::clamp(jnum(it, "opacity", 100), 0.0, 100.0) * 2.55);
        std::string type = jstr(*obj, "type", "sprite");
        Canvas &cv = jbool(*obj, "hud", false) ? hud : cv0;
        if (type == "text") {
            const json t = obj->contains("text") ? (*obj)["text"] : json::object();
            if (!jstr(t, "font").empty()) { drawFontText(cv, doc, jstr(t, "font"), resolveText(doc, jstr(t, "content", "Texto")), x, y, (float)jnum(t, "size", 24) * std::fabs(sy),
                                                         parseColor(jstr(t, "color", "#ffffff")), alpha, jbool(t, "bold", true), jstr(t, "align", "left").c_str()); continue; }
            drawText(cv, resolveText(doc, jstr(t, "content", "Texto")), x, y, (float)jnum(t, "size", 24) * std::fabs(sy),
                     parseColor(jstr(t, "color", "#ffffff")), alpha, jstr(t, "align", "left").c_str(), true);
        } else if (type == "button") {
            const json b = obj->contains("button") ? (*obj)["button"] : json::object();
            float hw = (float)jnum(b, "width", 160) * std::fabs(sx) / 2, hh = (float)jnum(b, "height", 42) * std::fabs(sy) / 2;
            fillRect(cv, x - hw, y - hh, x + hw, y + hh, parseColor(jstr(b, "bg", "#4a76c4")), alpha);
            if (!jstr(b, "font").empty()) { drawFontText(cv, doc, jstr(b, "font"), jstr(b, "text", "Boton"), x, y, (float)jnum(b, "fontSize", 16), parseColor(jstr(b, "fg", "#ffffff")), alpha, jbool(b, "bold", true), "center"); continue; }
            drawText(cv, jstr(b, "text", "Boton"), x, y, (float)jnum(b, "fontSize", 16), parseColor(jstr(b, "fg", "#ffffff")), alpha, "center", true);
        } else {
            const json *sp = byId(doc, "sprites", jstr(*obj, "spriteId"));
            if (!sp || !sp->contains("frames") || !(*sp)["frames"].is_array() || (*sp)["frames"].empty()) continue;
            int sw = (int)jnum(*sp, "width", 16), sh = (int)jnum(*sp, "height", 16), nf = (int)(*sp)["frames"].size();
            int f = (((int)jnum(it, "frame", 0)) % nf + nf) % nf;
            std::string key = "s:" + jstr(*sp, "id") + ":" + std::to_string(f);
            auto c = cache.find(key);
            const json &fr = (*sp)["frames"][f];
            if (c == cache.end()) c = cache.emplace(key, parsePixels(fr.contains("pixels") ? fr["pixels"] : json(), sw * sh)).first;
            drawImage(cv, c->second, sw, sh, x, y, sw, sh, sx, sy, (float)jnum(it, "angle", 0), alpha);
        }
    }
}

namespace {
struct Thumb { ui::PixTexture tex; long long stamp = -1; double at = 0; bool failed = false; std::string name; };
std::map<std::string, Thumb> g_files, g_docs;
int g_budget = 1;

void upload(Thumb &t, const std::vector<Px> &px) {
    t.tex.update(px, kThumbW, kThumbH);
    SetTextureFilter(t.tex.tex, TEXTURE_FILTER_BILINEAR);   // se muestra algo mas pequena que el original
}
}  // namespace

void thumbsNewFrame() { g_budget = 1; }

ui::PixTexture *fileThumb(const std::string &path) {
    Thumb &t = g_files[path];
    std::error_code ec;
    auto mt = fs::last_write_time(path, ec);
    long long stamp = ec ? 0 : (long long)mt.time_since_epoch().count();
    if (t.stamp != stamp) {
        if (g_budget <= 0) return t.tex.tex.id && !t.failed ? &t.tex : nullptr;
        g_budget--;
        t.stamp = stamp;
        Project p;
        std::string err;
        t.failed = ec || !p.load(path, err);
        if (!t.failed) { std::vector<Px> px; renderProjectThumb(p.doc, kThumbW, kThumbH, px); upload(t, px); t.name = p.name(); }
    }
    return t.failed || !t.tex.tex.id ? nullptr : &t.tex;
}

std::string fileProjectName(const std::string &path) {
    auto it = g_files.find(path);
    return it != g_files.end() && !it->second.name.empty() ? it->second.name : fs::path(path).stem().stem().string();
}

ui::PixTexture *docThumb(const std::string &key, const json &doc, int rev, bool live) {
    Thumb &t = g_docs[key];
    double now = GetTime();
    if ((t.stamp != rev || (live && now - t.at > 1.5)) && (g_budget > 0 || !t.tex.tex.id)) {
        g_budget--;
        t.stamp = rev; t.at = now;
        std::vector<Px> px;
        renderProjectThumb(doc, kThumbW, kThumbH, px);
        upload(t, px);
    }
    return &t.tex;
}
