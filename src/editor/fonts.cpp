#include "fonts.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include "project_docs.hpp"
#include "imgui_internal.h"
#include "raylib.h"

const char *kDefaultFontChars =
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~¡¿áéíóúüñÁÉÍÓÚÜÑ";

static std::vector<std::string> splitChars(const std::string &s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

static const json *fontById(const json &doc, const std::string &id) {
    if (id.empty() || !doc.contains("fonts") || !doc["fonts"].is_array()) return nullptr;
    for (auto &f : doc["fonts"]) if (jstr(f, "id") == id) return &f;
    return nullptr;
}

/* ---------------- fuentes TTF con raylib (vistas del editor) ---------------- */
// raylib rasteriza con stb_truetype a un tamano fijo: se guarda una fuente por (id, tamano)
static std::map<std::string, Font> g_rlFonts;

static Font *rlFont(const json &doc, const std::string &fontId, int size) {
    size = std::clamp(size, 4, 256);
    std::string key = fontId + "@" + std::to_string(size);
    auto it = g_rlFonts.find(key);
    if (it != g_rlFonts.end()) return it->second.glyphCount ? &it->second : nullptr;
    std::vector<unsigned char> bytes;
    const unsigned char *data = nullptr;
    int len = 0;
    const char *ext = ".ttf";
    if (fontId.empty()) data = ui::gameFontTTF(&len);
    else if (const json *f = fontById(doc, fontId)) {
        std::string raw = jstr(*f, "data");
        size_t comma = raw.find(',');
        bytes = base64Decode(comma == std::string::npos ? raw : raw.substr(comma + 1));
        data = bytes.data(); len = (int)bytes.size();
        if (jstr(*f, "mime").find("otf") != std::string::npos) ext = ".otf";
    }
    Font font{};
    if (data && len > 12) {
        std::vector<int> cps;
        for (int c = 32; c < 256; c++) cps.push_back(c);
        font = LoadFontFromMemory(ext, data, len, size, cps.data(), (int)cps.size());
    }
    if (g_rlFonts.size() > 48) { for (auto &kv : g_rlFonts) if (kv.second.glyphCount) UnloadFont(kv.second); g_rlFonts.clear(); }
    g_rlFonts[key] = font;
    return font.glyphCount ? &g_rlFonts[key] : nullptr;
}

/* ---------------- fuentes de pixeles ---------------- */
static int glyphAdvance(const json &f, int gi) {
    int W = std::max(1, (int)jnum(f, "cellW", 8)), H = std::max(1, (int)jnum(f, "cellH", 12));
    if (jbool(f, "mono", false)) return W;
    int right = -1;
    if (f.contains("glyphs") && gi >= 0 && gi < (int)f["glyphs"].size() && f["glyphs"][gi].contains("pixels")) {
        const json &px = f["glyphs"][gi]["pixels"];
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            size_t k = (size_t)(y * W + x);
            if (k < px.size() && px[k].is_string() && !px[k].get<std::string>().empty()) right = std::max(right, x);
        }
    }
    return right < 0 ? std::max(1, (int)std::lround(W / 2.0)) : right + 1;
}

static int glyphIndex(const std::vector<std::string> &chars, const std::string &ch) {
    for (size_t i = 0; i < chars.size(); i++) if (chars[i] == ch) return (int)i;
    if (ch.size() == 1) {   // sin la letra: prueba en mayusculas o minusculas
        char alt = std::isupper((unsigned char)ch[0]) ? (char)std::tolower((unsigned char)ch[0]) : (char)std::toupper((unsigned char)ch[0]);
        for (size_t i = 0; i < chars.size(); i++) if (chars[i].size() == 1 && chars[i][0] == alt) return (int)i;
    }
    return -1;
}

float pixelTextWidth(const json &f, const std::string &text) {
    auto chars = splitChars(jstr(f, "chars")), cs = splitChars(text);
    float sp = (float)jnum(f, "spacing", 1), w = 0;
    for (size_t i = 0; i < cs.size(); i++) {
        int gi = glyphIndex(chars, cs[i]);
        w += (gi < 0 ? std::max(1.0f, std::round((float)jnum(f, "cellW", 8) / 2)) : (float)glyphAdvance(f, gi)) + (i + 1 < cs.size() ? sp : 0);
    }
    return w;
}

bool renderTextImage(const json &doc, const std::string &fontId, const std::string &text, float size, Px color, bool bold, TextImage &out) {
    if (text.empty()) return false;
    const json *f = fontById(doc, fontId);
    if (f && jstr(*f, "kind") == "pixel") {
        int W = std::max(1, (int)jnum(*f, "cellW", 8)), H = std::max(1, (int)jnum(*f, "cellH", 12));
        auto chars = splitChars(jstr(*f, "chars")), cs = splitChars(text);
        int tw = std::max(1, (int)std::ceil(pixelTextWidth(*f, text)));
        out.w = tw; out.h = H; out.scale = size / H;
        out.px.assign((size_t)tw * H, Px{0, 0, 0, 0});
        bool tint = jbool(*f, "tint", true);
        float sp = (float)jnum(*f, "spacing", 1), x = 0;
        for (auto &ch : cs) {
            int gi = glyphIndex(chars, ch);
            if (gi < 0) { x += std::max(1.0f, std::round(W / 2.0f)) + sp; continue; }
            const json &g = (*f)["glyphs"][gi];
            if (g.contains("pixels"))
                for (int yy = 0; yy < H; yy++) for (int xx = 0; xx < W; xx++) {
                    size_t k = (size_t)(yy * W + xx);
                    int dx = (int)x + xx;
                    if (k >= g["pixels"].size() || dx >= tw || !g["pixels"][k].is_string()) continue;
                    Px c = parseColor(g["pixels"][k].get<std::string>());
                    if (!c.a) continue;
                    out.px[(size_t)yy * tw + dx] = tint ? Px{color.r, color.g, color.b, (uint8_t)(c.a * color.a / 255)} : c;
                }
            x += glyphAdvance(*f, gi) + sp;
        }
        return true;
    }
    int ps = std::max(4, (int)std::lround(size));
    Font *font = rlFont(doc, f ? fontId : "", ps);
    if (!font) return false;
    Image im = ImageTextEx(*font, text.c_str(), (float)ps, 0, Color{color.r, color.g, color.b, color.a});
    if (!im.data || im.width <= 0) { UnloadImage(im); return false; }
    ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    int ow = im.width + (bold ? 1 : 0);
    out.w = ow; out.h = im.height; out.scale = size / ps;
    out.px.assign((size_t)ow * im.height, Px{0, 0, 0, 0});
    const Px *src = (const Px *)im.data;
    for (int pass = 0; pass < (bold ? 2 : 1); pass++)
        for (int y = 0; y < im.height; y++) for (int x = 0; x < im.width; x++) {
            Px c = src[(size_t)y * im.width + x];
            Px &d = out.px[(size_t)y * ow + x + pass];
            if (c.a > d.a) d = c;
        }
    UnloadImage(im);
    return true;
}

/* ---------------- texturas de texto para las vistas ---------------- */
struct TextTex { ui::PixTexture tex; float scale = 1; int used = 0; };
static std::map<std::string, TextTex> g_texts;
static int g_frame = 0;

ui::PixTexture *textTexture(Project &proj, const std::string &fontId, const std::string &text, float size, const std::string &color, bool bold, float *scale) {
    if (text.empty()) return nullptr;
    std::string key = fontId + "|" + std::to_string((int)std::lround(size * 4)) + "|" + color + "|" + (bold ? "b" : "") + "|" + std::to_string(proj.rev) + "|" + text;
    auto it = g_texts.find(key);
    if (it == g_texts.end()) {
        TextImage img;
        if (!renderTextImage(proj.doc, fontId, text, size, parseColor(color), bold, img)) return nullptr;
        TextTex &t = g_texts[key];
        t.tex.update(img.px, img.w, img.h);
        const json *f = fontById(proj.doc, fontId);
        SetTextureFilter(t.tex.tex, f && jstr(*f, "kind") == "pixel" ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
        t.scale = img.scale;
        it = g_texts.find(key);
    }
    it->second.used = g_frame;
    if (scale) *scale = it->second.scale;
    return &it->second.tex;
}

void fontsNewFrame() {
    if (++g_frame % 120) return;
    for (auto it = g_texts.begin(); it != g_texts.end();) {
        if (g_frame - it->second.used > 240) { it->second.tex.release(); it = g_texts.erase(it); }
        else ++it;
    }
}

void releaseFontCache() {
    for (auto &kv : g_texts) kv.second.tex.release();
    g_texts.clear();
    for (auto &kv : g_rlFonts) if (kv.second.glyphCount) UnloadFont(kv.second);
    g_rlFonts.clear();
}

/* ---------------- crear e importar ---------------- */
void fillGlyphsFromGameFont(json &font) {
    int W = std::max(1, (int)jnum(font, "cellW", 8)), H = std::max(1, (int)jnum(font, "cellH", 12));
    auto chars = splitChars(jstr(font, "chars"));
    // se mide la fuente a 64 px: las letras y numeros (con los rabos de g, p, y) llenan la altura de la celda;
    // las mayusculas con tilde pueden perder la punta si la celda es baja
    float top = 1e9f, bottom = -1e9f, wide = 0;
    int nw = 0;
    if (Font *big = rlFont(json::object(), "", 64))
        for (int i = 0; i < big->glyphCount; i++) {
            int cp = big->glyphs[i].value;
            if (cp > 126 || !std::isalnum(cp)) continue;   // solo letras y numeros: los simbolos como | o [ son mas altos
            top = std::min(top, (float)big->glyphs[i].offsetY);
            bottom = std::max(bottom, big->glyphs[i].offsetY + big->recs[i].height);
            wide += (float)big->recs[i].width; nw++;
        }
    wide = nw ? wide / nw : 32;   // ancho medio: las letras muy anchas (M, W) se pueden salir un poco
    float k = bottom > top ? std::min(H / (bottom - top), (W - 1.0f) / wide) : H / 64.0f;
    int fs = std::max(4, (int)std::floor(64 * k));
    float oy = bottom > top ? -top * fs / 64.0f : 0;
    Font *f = rlFont(json::object(), "", fs);
    json glyphs = json::array();
    for (auto &ch : chars) {
        json px = json::array();
        Image img = GenImageColor(W, H, BLANK);
        if (f && ch != " ") ImageDrawTextEx(&img, *f, ch.c_str(), Vector2{0, std::round(oy)}, (float)fs, 0, WHITE);
        const Color *c = (const Color *)img.data;
        for (int i = 0; i < W * H; i++) px.push_back(c[i].a >= 110 ? "#ffffff" : "");
        UnloadImage(img);
        glyphs.push_back({{"pixels", px}});
    }
    font["glyphs"] = glyphs;
}

std::string createPixelFont(Project &proj, const std::string &name, int cellW, int cellH, bool fromGameFont) {
    if (!proj.doc.contains("fonts") || !proj.doc["fonts"].is_array()) proj.doc["fonts"] = json::array();
    std::string id = makeUid("f");
    json f = {{"id", id}, {"name", name}, {"kind", "pixel"}, {"cellW", cellW}, {"cellH", cellH}, {"chars", kDefaultFontChars},
              {"spacing", 1}, {"mono", false}, {"tint", true}};
    if (fromGameFont) fillGlyphsFromGameFont(f);
    else {
        json glyphs = json::array();
        for (size_t i = 0; i < splitChars(kDefaultFontChars).size(); i++) glyphs.push_back({{"pixels", json(std::vector<std::string>((size_t)cellW * cellH, ""))}});
        f["glyphs"] = glyphs;
    }
    proj.doc["fonts"].push_back(f);
    proj.dirty = true; proj.rev++;
    return id;
}

std::string importFont(Project &proj, const std::string &file, std::string &err) {
    std::ifstream in(file, std::ios::binary);
    if (!in) { err = "No se pudo abrir " + file; return ""; }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // cabecera de TrueType (00 01 00 00 o 'true') u OpenType ('OTTO')
    bool ok = data.size() > 12 && ((data[0] == 0 && data[1] == 1 && data[2] == 0 && data[3] == 0) || !memcmp(data.data(), "OTTO", 4) || !memcmp(data.data(), "true", 4));
    if (!ok) { err = "No parece una fuente TTF u OTF"; return ""; }
    std::string base = file.substr(file.find_last_of('/') + 1), ext = base.substr(base.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (!proj.doc.contains("fonts") || !proj.doc["fonts"].is_array()) proj.doc["fonts"] = json::array();
    std::string id = makeUid("f");
    proj.doc["fonts"].push_back({{"id", id}, {"name", base.substr(0, base.find_last_of('.'))}, {"kind", "ttf"},
                                 {"mime", ext == "otf" ? "font/otf" : "font/ttf"}, {"data", base64Encode(data.data(), data.size())}});
    proj.dirty = true; proj.rev++;
    return id;
}

/* ---------------- editor de fuentes ---------------- */
std::string FontEditor::title(Project &proj) {
    const json *f = fontById(proj.doc, fontId);
    return "Fuente: " + (f ? jstr(*f, "name") : std::string("?")) + "###font_" + fontId;
}

static json *fontRef(Project &proj, const std::string &id) {
    if (!proj.doc.contains("fonts") || !proj.doc["fonts"].is_array()) return nullptr;
    return findById(proj.doc["fonts"], id);
}

void FontEditor::reload(Project &proj) {
    json *f = fontRef(proj, fontId);
    if (!f || jstr(*f, "kind") != "pixel") return;
    json sp = {{"id", fontId}, {"name", jstr(*f, "name")}, {"width", (int)jnum(*f, "cellW", 8)}, {"height", (int)jnum(*f, "cellH", 12)}, {"frames", json::array()}};
    if (f->contains("glyphs")) for (auto &g : (*f)["glyphs"]) sp["frames"].push_back({{"pixels", g.contains("pixels") ? g["pixels"] : json::array()}});
    if (sp["frames"].empty()) sp["frames"].push_back({{"pixels", json::array()}});
    int keep = pix.curFrame;
    pix.load(sp);
    pix.tileMode = true;
    pix.curFrame = std::clamp(keep, 0, std::max(0, (int)pix.st.layers[0].cels.size() - 1));
    loaded = true;
}

void FontEditor::writeBack(Project &proj) {
    json *f = fontRef(proj, fontId);
    if (!f) return;
    json glyphs = json::array();
    std::vector<Px> comp;
    for (int i = 0; i < (int)pix.st.layers[0].cels.size(); i++) {
        pix.composite(i, comp, false);
        json px = json::array();
        for (auto &c : comp) px.push_back(colorToString(c));
        glyphs.push_back({{"pixels", px}});
    }
    (*f)["glyphs"] = glyphs;
    (*f)["cellW"] = pix.st.w; (*f)["cellH"] = pix.st.h;
    proj.dirty = true; proj.rev++;
}

void FontEditor::setCell(Project &proj, int W, int H) {
    json *f = fontRef(proj, fontId);
    if (!f) return;
    writeBack(proj);
    W = std::clamp(W, 2, 64); H = std::clamp(H, 2, 64);
    int oW = (int)jnum(*f, "cellW", 8), oH = (int)jnum(*f, "cellH", 12);
    for (auto &g : (*f)["glyphs"]) {   // se conserva la esquina superior izquierda, como en los tiles
        json np = json::array();
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            size_t k = (size_t)(y * oW + x);
            np.push_back(x < oW && y < oH && g.contains("pixels") && k < g["pixels"].size() ? g["pixels"][k] : json(""));
        }
        g["pixels"] = np;
    }
    (*f)["cellW"] = W; (*f)["cellH"] = H;
    proj.dirty = true; proj.rev++;
    reload(proj);
}

void FontEditor::setChars(Project &proj, const std::string &chars) {
    json *f = fontRef(proj, fontId);
    if (!f) return;
    writeBack(proj);
    auto oldC = splitChars(jstr(*f, "chars")), newC = splitChars(chars);
    int W = (int)jnum(*f, "cellW", 8), H = (int)jnum(*f, "cellH", 12);
    json glyphs = json::array();
    for (auto &c : newC) {   // cada letra conserva su dibujo; las nuevas empiezan vacias
        auto it = std::find(oldC.begin(), oldC.end(), c);
        size_t oi = (size_t)(it - oldC.begin());
        if (it != oldC.end() && oi < (*f)["glyphs"].size()) glyphs.push_back((*f)["glyphs"][oi]);
        else glyphs.push_back({{"pixels", json(std::vector<std::string>((size_t)W * H, ""))}});
    }
    (*f)["chars"] = chars; (*f)["glyphs"] = glyphs;
    proj.dirty = true; proj.rev++;
    reload(proj);
}

void FontEditor::draw(Project &proj) {
    json *f = fontRef(proj, fontId);
    if (!f) { open = false; return; }
    bool pixel = jstr(*f, "kind") == "pixel";
    if (pixel && !loaded) reload(proj);
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title(proj).c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##fleft", ImVec2(pixel ? 320 : 0, avail.y));
    ImGui::SeparatorText("Fuente");
    ImGui::SetNextItemWidth(-1);
    if (editText("##fname", *f, "name")) proj.dirty = true;
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled(pixel ? "Fuente de píxeles: cada letra es un dibujo, como un tile." : "Fuente TTF/OTF importada.");
    ImGui::TextDisabled("Elígela en las propiedades de un objeto de texto o de un botón.");
    ImGui::PopTextWrapPos();
    if (pixel) {
        int W = (int)jnum(*f, "cellW", 8), H = (int)jnum(*f, "cellH", 12), nW = W, nH = H;
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Celda"); ImGui::SameLine(110); ImGui::SetNextItemWidth(90); ImGui::InputInt("##cw", &nW);
        ImGui::SameLine(); ImGui::TextUnformatted("x"); ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::InputInt("##ch", &nH);
        if (nW != W || nH != H) setCell(proj, nW, nH);
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Separación"); ImGui::SameLine(110); ImGui::SetNextItemWidth(90);
        if (editNum("##sp", *f, "spacing", 1, 0.1f, "%.0f px", -4, 16)) proj.rev++;
        if (editBool("Monoespaciada (todas las letras igual de anchas)", *f, "mono", false)) proj.rev++;
        if (editBool("Tomar el color del texto", *f, "tint", true)) proj.rev++;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Desactívalo para que la fuente conserve los colores con los que la dibujaste");
        ImGui::SeparatorText("Letras");
        static char chbuf[1024];
        if (ImGui::IsWindowAppearing() || ImGui::GetActiveID() != ImGui::GetID("##chars")) snprintf(chbuf, sizeof chbuf, "%s", jstr(*f, "chars").c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##chars", chbuf, sizeof chbuf, ImGuiInputTextFlags_EnterReturnsTrue)) setChars(proj, chbuf);
        if (ImGui::IsItemHovered() && ImGui::GetActiveID() != ImGui::GetID("##chars")) {   // la lista entera, que en el campo no cabe
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(420); ImGui::TextUnformatted(chbuf); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
        ImGui::TextDisabled("Escribe los caracteres de la fuente y pulsa Enter.");
        if (ImGui::Button("Rellenar con la fuente del juego", ImVec2(-1, 0))) { writeBack(proj); fillGlyphsFromGameFont(*f); proj.rev++; reload(proj); }
        auto chars = splitChars(jstr(*f, "chars"));
        float cell = 26;
        int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 3) / (cell + 3)));
        ImGui::BeginChild("##glyphs", ImVec2(0, std::min(260.0f, ((float)chars.size() / perRow + 1) * (cell + 3) + 4)));
        for (int i = 0; i < (int)chars.size(); i++) {
            ImGui::PushID(i);
            if (i % perRow) ImGui::SameLine(0, 3);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##g", ImVec2(cell, cell))) pix.curFrame = i;
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), i == pix.curFrame ? ui::kPrimary : ui::kPanel2, 4);
            ImVec2 ts = ImGui::CalcTextSize(chars[i].c_str());
            dl->AddText(ImVec2(p.x + (cell - ts.x) / 2, p.y + (cell - ts.y) / 2), ui::kText, chars[i] == " " ? "·" : chars[i].c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Letra '%s'", chars[i].c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::SeparatorText("Vista previa");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##sample", sample, sizeof sample);
    std::string col = pixel && !jbool(*f, "tint", true) ? "#ffffff" : "#f0e6d2";
    for (float sz : {16.0f, 32.0f, 48.0f}) {
        float sc = 1;
        ui::PixTexture *tx = textTexture(proj, fontId, sample, sz, col, false, &sc);
        if (!tx) { ImGui::TextDisabled("(sin texto)"); break; }
        float w = tx->w * sc, h = tx->h * sc;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h + 6));
        ImGui::GetWindowDrawList()->AddImage(tx->id(), p, ImVec2(p.x + w, p.y + h));
    }
    ImGui::EndChild();
    if (pixel) {
        ImGui::SameLine();
        ImGui::BeginChild("##fpix", ImVec2(0, avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        pix.drawBody(proj);
        ImGui::EndChild();
    }
    ImGui::End();
    if (pix.dirtyForProject) { writeBack(proj); pix.dirtyForProject = false; }
}
