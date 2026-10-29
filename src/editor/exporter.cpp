// Source 2D - exportar juegos (ver exporter.hpp)
#include "exporter.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <regex>
#include "raylib.h"
#include "players_bin.h"
#include "assets.hpp"
#include "lzdec.h"
#include "lzenc.h"
#include "project_docs.hpp"
#include "runtime_js.h"
#include "gfx_web_js.h"

namespace exporter {

static constexpr long kTail = 56;        // cola del juego ligero (src/stub.c)

struct Bin { const unsigned char *data; unsigned long len, raw, props; };
static Bin binFor(const std::string &os, const char *what) {
    bool win = os == "windows";
    if (!strcmp(what, "stub")) return win ? Bin{stub_win, stub_win_len, stub_win_raw, 0} : Bin{stub_linux, stub_linux_len, stub_linux_raw, 0};
    if (!strcmp(what, "lite")) return win ? Bin{lite_win, lite_win_len, lite_win_raw, lite_win_props} : Bin{lite_linux, lite_linux_len, lite_linux_raw, lite_linux_props};
    return win ? Bin{full_win, full_win_len, full_win_raw, full_win_props} : Bin{full_linux, full_linux_len, full_linux_raw, full_linux_props};
}

bool available(const std::string &os, bool lite) {
    if (os == "html") return true;
    if (!binFor(os, "full").len) return false;
    return !lite || binFor(os, "stub").len > 0;
}

std::string kb(long b) {
    char s[32];
    if (b >= 1024 * 1024) snprintf(s, sizeof s, "%.1f MB", b / 1048576.0);
    else snprintf(s, sizeof s, "%.0f KB", b / 1024.0);
    return s;
}

/* ---------------- que necesita el proyecto ---------------- */
// formato de un dato incrustado en base64 (con o sin "data:...,") por sus primeros bytes
static const char *b64Kind(const std::string &s) {
    if (s.size() < 64) return nullptr;
    size_t c = s.rfind("data:", 0) == 0 ? s.find(',') : std::string::npos;
    const char *p = s.c_str() + (c == std::string::npos ? 0 : c + 1);
    static const struct { const char *magic, *kind; } K[] = {
        {"iVBORw0KGgo", nullptr}, {"T2dnUw", nullptr}, {"UklGR", nullptr},          // PNG, OGG, WAV: los tiene el ligero
        {"/9j/", "imágenes JPG"}, {"R0lGOD", "imágenes GIF"}, {"Qk", "imágenes BMP"}, {"cW9pZg", "imágenes QOI"},
        {"SUQz", "audio MP3"}, {"//u", "audio MP3"}, {"//s", "audio MP3"}, {"//v", "audio MP3"}, {"//7", "audio MP3"},
        {"ZkxhQw", "audio FLAC"}, {"cW9hZg", "audio QOA"},
    };
    for (auto &k : K) if (!strncmp(p, k.magic, strlen(k.magic))) return k.kind;
    return nullptr;
}
static bool beyondLatin1(const std::string &s) {
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) continue;
        unsigned cp = 0; int n = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; } else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; } else if ((c & 0xF8) == 0xF0) { cp = c & 7; n = 3; }
        for (int k = 0; k < n && i + 1 < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[++i] & 0x3F);
        if (cp > 0xFF && cp != 0x20AC) return true;
    }
    return false;
}
static void scan(const json &j, std::string &why) {
    if (!why.empty()) return;
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end() && why.empty(); ++it)
            if (it.key() != "chars") scan(it.value(), why);   // letras de las fuentes de pixeles: no usan la del motor
        return;
    }
    if (j.is_array()) { for (auto &v : j) { scan(v, why); if (!why.empty()) return; } return; }
    if (!j.is_string()) return;
    const std::string &s = j.get_ref<const std::string &>();
    if (const char *k = b64Kind(s)) { why = k; return; }
    if (s.size() >= 64 && s.find(' ') == std::string::npos && s.find('\n') == std::string::npos) return;   // datos en base64
    static const std::regex js("\\b(Date|Proxy|Atomics|BigInt)\\b");
    if (s.find("Date") != std::string::npos || s.find("Proxy") != std::string::npos || s.find("Atomics") != std::string::npos ||
        s.find("BigInt") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(s, m, js)) { why = "código que usa " + m[1].str(); return; }
    }
    if (beyondLatin1(s)) why = "letras fuera del alfabeto latino";
}
bool needsFull(const json &doc, std::string &why) {
    why.clear();
    // la musica puede ir en base64 sin "data:": su formato lo dice el campo mime
    if (doc.contains("music") && doc["music"].is_array())
        for (auto &m : doc["music"]) {
            std::string mime = m.is_object() ? m.value("mime", "") : "";
            for (auto &c : mime) c = (char)tolower((unsigned char)c);
            if (mime.find("mp3") != std::string::npos || mime.find("mpeg") != std::string::npos) { why = "audio MP3"; return true; }
            if (mime.find("flac") != std::string::npos) { why = "audio FLAC"; return true; }
            if (mime.find("qoa") != std::string::npos) { why = "audio QOA"; return true; }
        }
    scan(doc, why);
    return !why.empty();
}

/* imagenes de pixeles (listas de "#rrggbb" o null) como paleta + un caracter por pixel: el runtime las vuelve a
   expandir al cargar (unpackPixels en src/runtime.js). Indice i -> caracter 35 + i, saltando la barra invertida. */
static void packPixels(json &j) {
    if (j.is_array()) { for (auto &v : j) if (v.is_structured()) packPixels(v); return; }
    if (!j.is_object()) return;
    auto it = j.find("pixels");
    if (it != j.end() && it->is_array() && !it->empty()) {
        std::vector<std::string> pal;
        std::string ix;
        ix.reserve(it->size());
        bool ok = true;
        for (auto &p : *it) {
            if (!p.is_null() && !p.is_string()) { ok = false; break; }
            std::string c = p.is_null() ? std::string() : p.get<std::string>();
            size_t k = std::find(pal.begin(), pal.end(), c) - pal.begin();
            if (k == pal.size()) { if (pal.size() == 91) { ok = false; break; } pal.push_back(c); }
            int code = 35 + (int)k;
            ix += (char)(code >= 92 ? code + 1 : code);
        }
        if (ok) {
            json jp = json::array();
            for (auto &c : pal) jp.push_back(c.empty() ? json(nullptr) : json(c));
            j.erase("pixels");
            j["pal"] = jp;
            j["ix"] = ix;
        }
    }
    for (auto &kv : j.items()) if (kv.value().is_structured()) packPixels(kv.value());
}
/* Datos en base64 (imagenes, audio, fuentes) -> binario aparte, sin perdida: en el proyecto queda una marca
   "\u0001S2DB:<inicio>:<largo>:<prefijo>" que el jugador (src/main.c, expand_blobs) vuelve a cambiar por
   prefijo + base64 de esos bytes. Solo se cambia si al volver a base64 sale exactamente el mismo texto. */
static const char *kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static bool b64Decode(const char *s, size_t n, std::string &out) {
    static int8_t inv[256];
    static bool ready = false;
    if (!ready) { memset(inv, -1, sizeof inv); for (int i = 0; i < 64; i++) inv[(unsigned char)kB64[i]] = (int8_t)i; ready = true; }
    if (n % 4) return false;
    out.clear(); out.reserve(n / 4 * 3);
    for (size_t i = 0; i < n; i += 4) {
        int a = inv[(unsigned char)s[i]], b = inv[(unsigned char)s[i + 1]];
        int c = s[i + 2] == '=' ? 0 : inv[(unsigned char)s[i + 2]], d = s[i + 3] == '=' ? 0 : inv[(unsigned char)s[i + 3]];
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        if ((s[i + 2] == '=' || s[i + 3] == '=') && i + 4 != n) return false;
        unsigned v = (unsigned)a << 18 | (unsigned)b << 12 | (unsigned)c << 6 | (unsigned)d;
        out += (char)(v >> 16);
        if (s[i + 2] != '=') out += (char)(v >> 8 & 255);
        if (s[i + 3] != '=') out += (char)(v & 255);
    }
    // sin perdida: el jugador lo vuelve a codificar con relleno '=' normal; si el original era distinto, no se toca
    size_t want = (out.size() + 2) / 3 * 4;
    return want == n && (n < 4 || (s[n - 1] == '=') == (out.size() % 3 != 0));
}
// formatos que ya vienen comprimidos: LZMA no los reduce y solo tardaria
static bool alreadyCompressed(const std::string &b) {
    const unsigned char *p = (const unsigned char *)b.data();
    if (b.size() < 4) return false;
    return !memcmp(p, "OggS", 4) || !memcmp(p, "\x89PNG", 4) || (p[0] == 0xFF && p[1] == 0xD8) || !memcmp(p, "GIF8", 4) ||
           !memcmp(p, "fLaC", 4) || !memcmp(p, "qoaf", 4) || !memcmp(p, "ID3", 3) || (p[0] == 0xFF && (p[1] & 0xE0) == 0xE0) ||
           !memcmp(p, "qoif", 4) || !memcmp(p, "wOFF", 4) || !memcmp(p, "PK", 2);
}
struct Blobs { std::vector<std::pair<std::string *, std::string>> soft, hard; };   // (valor del proyecto, bytes)
static void collectBlobs(json &j, Blobs &bl) {
    if (j.is_structured()) { for (auto &v : j) collectBlobs(v, bl); return; }
    if (!j.is_string()) return;
    std::string &s = j.get_ref<std::string &>();
    if (s.size() < 256) return;
    size_t start = 0;
    if (s.rfind("data:", 0) == 0) {
        size_t c = s.find(',');
        if (c == std::string::npos || c < 7 || s.compare(c - 7, 7, ";base64") != 0) return;
        start = c + 1;
    }
    std::string bin;
    if (!b64Decode(s.data() + start, s.size() - start, bin)) return;
    (alreadyCompressed(bin) ? bl.hard : bl.soft).push_back({&s, std::move(bin)});
}
Packed packed(const json &doc, bool pixels) {
    json d = doc;
    if (pixels) packPixels(d);
    Blobs bl;
    collectBlobs(d, bl);
    Packed out;
    size_t off = 0;
    for (auto *list : {&bl.soft, &bl.hard})
        for (auto &e : *list) {
            std::string prefix = e.first->rfind("data:", 0) == 0 ? e.first->substr(0, e.first->find(',') + 1) : std::string();
            *e.first = "\x01S2DB:" + std::to_string(off) + ":" + std::to_string(e.second.size()) + ":" + prefix;
            (list == &bl.soft ? out.soft : out.hard) += e.second;
            off += e.second.size();
        }
    out.json = d.dump();
    return out;
}

/* LZMA propio (src/lzenc.h), comprobado al descomprimir; si algo no cuadra, deflate */
static const unsigned kProjProps = 3;     // lc 3, lp 0, pb 0: lo mejor para texto
static std::vector<unsigned char> pack(const void *data, size_t len, bool &lzma) {
    std::vector<unsigned char> out(len + len / 2 + 64);
    size_t n = lz_encode((const uint8_t *)data, len, out.data(), out.size(), kProjProps);
    if (n) {
        std::vector<unsigned char> back(len);
        if (lz_decode(back.data(), len, out.data(), n, kProjProps) == 0 && !memcmp(back.data(), data, len)) {
            out.resize(n);
            lzma = true;
            return out;
        }
    }
    int zn = 0;
    unsigned char *z = CompressData((const unsigned char *)data, (int)len, &zn);
    out.assign(z, z + (z ? zn : 0));
    MemFree(z);
    lzma = false;
    return out;
}

/* .source2d (Exportar > Solo para Source 2D): el proyecto con los datos en base64 pasados a binario y todo en LZMA,
   como la exportacion ligera, pero sin motor. Formato:
     "S2DPAK1\n", largo del JSON, largo de "soft", largo de "hard", largo comprimido (8 bytes cada uno, little endian),
     LZMA(JSON + soft) y "hard" tal cual (lo que ya viene comprimido, como OGG o PNG). */
static const char kPak[] = "S2DPAK1\n";
static void put64(std::string &o, unsigned long long v) { for (int i = 0; i < 8; i++) { o += (char)(v & 255); v >>= 8; } }
static unsigned long long get64(const unsigned char *p) { unsigned long long v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }
std::string sourcePack(const json &doc) {
    Packed pk = packed(doc, false);
    std::string plain = pk.json + pk.soft;
    std::vector<unsigned char> z(plain.size() + plain.size() / 2 + 64);
    size_t n = lz_encode((const uint8_t *)plain.data(), plain.size(), z.data(), z.size(), kProjProps);
    if (!n) return "";
    std::string out(kPak, 8);
    put64(out, pk.json.size()); put64(out, pk.soft.size()); put64(out, pk.hard.size()); put64(out, n);
    out.append((const char *)z.data(), n);
    out += pk.hard;
    return out;
}
bool isSourcePack(const std::string &b) { return b.size() >= 40 && !memcmp(b.data(), kPak, 8); }
// vuelve a poner cada marca "\u0001S2DB:inicio:largo:prefijo" como prefijo + base64
static void expandBlobs(json &j, const std::string &bin) {
    if (j.is_structured()) { for (auto &v : j) expandBlobs(v, bin); return; }
    if (!j.is_string()) return;
    const std::string &s = j.get_ref<const std::string &>();
    if (s.rfind("\x01S2DB:", 0) != 0) return;
    unsigned long long off = 0, len = 0;
    size_t p1 = s.find(':', 6), p2 = p1 == std::string::npos ? p1 : s.find(':', p1 + 1);
    if (p2 == std::string::npos) return;
    off = strtoull(s.c_str() + 6, nullptr, 10); len = strtoull(s.c_str() + p1 + 1, nullptr, 10);
    if (off + len > bin.size()) return;
    j = s.substr(p2 + 1) + base64Encode((const unsigned char *)bin.data() + off, (size_t)len);
}
bool sourceUnpack(const std::string &b, json &doc, std::string &err) {
    const unsigned char *p = (const unsigned char *)b.data();
    unsigned long long jl = get64(p + 8), sl = get64(p + 16), hl = get64(p + 24), zl = get64(p + 32);
    if (40 + zl + hl != b.size() || jl + sl > (1ULL << 33)) { err = "El archivo .source2d está dañado"; return false; }
    std::string plain(jl + sl, '\0');
    if (lz_decode((uint8_t *)plain.data(), plain.size(), p + 40, zl, kProjProps) != 0) { err = "El archivo .source2d está dañado"; return false; }
    doc = json::parse(plain.substr(0, jl), nullptr, false);
    if (doc.is_discarded()) { err = "El archivo .source2d está dañado"; return false; }
    expandBlobs(doc, plain.substr(jl) + b.substr(40 + zl));
    return true;
}

unsigned long playerSize(const std::string &os) { return binFor(os, "full").raw; }

bool unpackPlayer(const std::string &os, std::vector<unsigned char> &out) {
    Bin b = binFor(os, "full");
    if (!b.len) return false;
    out.resize(b.raw);
    if (lz_decode(out.data(), b.raw, b.data, b.len, (unsigned)(b.props & 255)) != 0) { out.clear(); return false; }
    if (b.props & 256) lz_x86_decode(out.data(), out.size());
    return true;
}

static void wr64(unsigned char *p, unsigned long long v) { for (int i = 0; i < 8; i++) { p[i] = (unsigned char)v; v >>= 8; } }

/* ---------------- web (HTML) ----------------
   Una pagina con todo dentro: el proyecto como JSON (datos, no codigo), src/web/gfx_web.js (lienzos, WebGL, audio y
   archivos del navegador) y el runtime de siempre. Con .zip, la pagina va como index.html: lo que piden itch.io y las
   game jams. */
static std::string htmlPage(const json &doc) {
    std::string pj = doc.dump(), safe;
    safe.reserve(pj.size() + 64);
    for (char c : pj) { if (c == '<') safe += "\\u003c"; else safe += c; }   // nada de "</script>" dentro del JSON
    std::string name = jstr(doc, "name", "Juego"), title;
    for (char c : name) { if (c == '<') title += "&lt;"; else if (c == '&') title += "&amp;"; else title += c; }
    std::string out;
    out += "<!DOCTYPE html>\n<html lang=\"es\">\n<head>\n<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n<title>" + title + "</title>\n"
           "<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}"
           "body{display:flex;align-items:center;justify-content:center}"
           "#s2d-pantalla{image-rendering:pixelated;image-rendering:crisp-edges;outline:none;touch-action:none}</style>\n"
           "</head>\n<body>\n<script type=\"application/json\" id=\"s2d-proyecto\">";
    out += safe;
    out += "</script>\n<script>\n";
    out.append((const char *)gfx_web_js, gfx_web_js_len);
    out += "\n</script>\n<script>\n";
    out.append((const char *)runtime_js, runtime_js_len);
    out += "\n</script>\n<!-- hecho con Source 2D -->\n</body>\n</html>\n";
    return out;
}
/* .zip con un solo archivo (index.html), comprimido con deflate */
static uint32_t crc32(const unsigned char *p, size_t n) {
    static uint32_t t[256];
    if (!t[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static std::string zipOne(const std::string &name, const std::string &data) {
    int zn = 0;
    unsigned char *z = CompressData((const unsigned char *)data.data(), (int)data.size(), &zn);   // deflate sin cabecera
    std::string comp = z ? std::string((const char *)z, (size_t)zn) : std::string();
    MemFree(z);
    bool stored = comp.empty() || comp.size() >= data.size();
    const std::string &body = stored ? data : comp;
    uint32_t crc = crc32((const unsigned char *)data.data(), data.size());
    auto u16 = [](std::string &s, unsigned v) { s += (char)(v & 255); s += (char)(v >> 8 & 255); };
    auto u32 = [](std::string &s, uint32_t v) { for (int i = 0; i < 4; i++) s += (char)(v >> (8 * i) & 255); };
    std::string out, cd;
    u32(out, 0x04034b50); u16(out, 20); u16(out, 0x0800); u16(out, stored ? 0 : 8); u16(out, 0); u16(out, 0x21);   // utf-8, 1980-01-01
    u32(out, crc); u32(out, (uint32_t)body.size()); u32(out, (uint32_t)data.size()); u16(out, (unsigned)name.size()); u16(out, 0);
    out += name; out += body;
    u32(cd, 0x02014b50); u16(cd, 20); u16(cd, 20); u16(cd, 0x0800); u16(cd, stored ? 0 : 8); u16(cd, 0); u16(cd, 0x21);
    u32(cd, crc); u32(cd, (uint32_t)body.size()); u32(cd, (uint32_t)data.size()); u16(cd, (unsigned)name.size());
    u16(cd, 0); u16(cd, 0); u16(cd, 0); u16(cd, 0); u32(cd, 0); u32(cd, 0); cd += name;
    uint32_t cdOff = (uint32_t)out.size();
    out += cd;
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, 1); u16(out, 1); u32(out, (uint32_t)cd.size()); u32(out, cdOff); u16(out, 0);
    return out;
}
static bool exportHtml(const json &doc, const std::string &out, std::string &err, std::string &info) {
    std::string page = htmlPage(doc);
    bool zip = out.size() > 4 && (out.compare(out.size() - 4, 4, ".zip") == 0 || out.compare(out.size() - 4, 4, ".ZIP") == 0);
    std::string data = zip ? zipOne("index.html", page) : page;
    FILE *f = fopen(out.c_str(), "wb");
    bool ok = f && fwrite(data.data(), 1, data.size(), f) == data.size();
    if (f) ok = fclose(f) == 0 && ok;
    if (!ok) { err = "No se pudo escribir " + out; return false; }
    info = kb((long)data.size()) + (zip ? ", web: index.html dentro del .zip" : ", web: una sola pagina");
    return true;
}

bool exportGame(const json &doc, const std::string &out, const std::string &os, bool lite, std::string &err, std::string &info) {
    if (os == "html") return exportHtml(doc, out, err, info);
    bool win = os == "windows";
    const char *sys = win ? "Windows" : "Linux";
    if (!available(os, lite)) { err = std::string("Este editor no incluye el motor para ") + sys; return false; }
    Packed pk = packed(doc);
    std::string pj = pk.json;
    if (!pk.soft.empty() || !pk.hard.empty()) { pj += '\0'; pj += pk.soft; }   /* el proyecto y despues sus datos en binario */
    FILE *f = nullptr;
    bool ok = false;
    if (!lite) {
        /* motor completo tal cual + [proyecto][uint64 largo]["S2DPAYLD"] (lo lee src/main.c) */
        std::vector<unsigned char> pl;
        if (!unpackPlayer(os, pl)) { err = "No se pudo preparar el motor"; return false; }
        unsigned char tr[16];
        wr64(tr, pj.size() + pk.hard.size()); memcpy(tr + 8, "S2DPAYLD", 8);
        f = fopen(out.c_str(), "wb");
        ok = f && fwrite(pl.data(), 1, pl.size(), f) == pl.size() && fwrite(pj.data(), 1, pj.size(), f) == pj.size() &&
             fwrite(pk.hard.data(), 1, pk.hard.size(), f) == pk.hard.size() && fwrite(tr, 1, 16, f) == 16;
        info = "motor completo";
    } else {
        /* [lanzador][motor en LZMA][proyecto y datos comprimibles en LZMA][datos ya comprimidos tal cual][cola de 56 bytes]
           (lo lee src/stub.c) */
        std::string why;
        bool full = needsFull(doc, why) || !binFor(os, "lite").len;
        Bin stub = binFor(os, "stub"), pl = binFor(os, full ? "full" : "lite");
        bool lz = false;
        std::vector<unsigned char> z = pack(pj.data(), pj.size(), lz);
        if (z.empty()) { err = "No se pudo comprimir el juego"; return false; }
        unsigned char tail[kTail];
        wr64(tail, pl.len); wr64(tail + 8, pl.raw); wr64(tail + 16, z.size()); wr64(tail + 24, pj.size() + pk.hard.size());
        wr64(tail + 32, pl.props | (lz ? (unsigned long long)(256 + kProjProps) << 16 : 0));   // bits 16..24: proyecto en LZMA
        wr64(tail + 40, pk.hard.size());
        memcpy(tail + 48, "S2DMINI3", 8);
        f = fopen(out.c_str(), "wb");
        ok = f && fwrite(stub.data, 1, stub.len, f) == stub.len && fwrite(pl.data, 1, pl.len, f) == pl.len &&
             fwrite(z.data(), 1, z.size(), f) == z.size() && fwrite(pk.hard.data(), 1, pk.hard.size(), f) == pk.hard.size() &&
             fwrite(tail, 1, kTail, f) == (size_t)kTail;
        info = full ? "motor completo comprimido, porque el juego usa " + (why.empty() ? std::string("lo que el ligero no tiene") : why) : "motor ligero";
    }
    if (f) ok = fclose(f) == 0 && ok;
    if (!ok) { err = "No se pudo escribir " + out; return false; }
    FILE *g = fopen(out.c_str(), "rb");
    long size = 0;
    if (g) { fseek(g, 0, SEEK_END); size = ftell(g); fclose(g); }
    info = kb(size) + ", " + info;
    return true;
}

}

namespace exporter {
/* a una carpeta: el motor sin nada dentro y el juego al lado, en datos/juego.source2d.json (el motor lo busca ahi);
   para la web, la pagina como index.html. Asi se puede cambiar el juego sin volver a exportar el motor. */
bool exportFolder(const json &doc, const std::string &dir, const std::string &os, const std::string &name, std::string &err, std::string &info) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(dir) / "datos", ec);
    if (ec) { err = "No se pudo crear la carpeta " + dir; return false; }
    if (os == "html") {
        fs::remove(fs::path(dir) / "datos", ec);
        return exportHtml(doc, (fs::path(dir) / "index.html").string(), err, info);
    }
    std::vector<unsigned char> eng;
    if (!unpackPlayer(os, eng) || eng.empty()) { err = "Este editor no lleva el motor de " + os; return false; }
    fs::path exe = fs::path(dir) / (name + (os == "windows" ? ".exe" : ".x86_64"));
    FILE *f = fopen(exe.string().c_str(), "wb");
    if (!f || fwrite(eng.data(), 1, eng.size(), f) != eng.size()) { if (f) fclose(f); err = "No se pudo escribir " + exe.string(); return false; }
    fclose(f);
    std::string js = doc.dump();
    f = fopen((fs::path(dir) / "datos" / "juego.source2d.json").string().c_str(), "wb");
    if (!f || fwrite(js.data(), 1, js.size(), f) != js.size()) { if (f) fclose(f); err = "No se pudo escribir datos/juego.source2d.json"; return false; }
    fclose(f);
    f = fopen((fs::path(dir) / "datos" / "LEEME.txt").string().c_str(), "wb");
    if (f) {
        fputs("Hecho con Source 2D.\n"
              "juego.source2d.json es el juego: el motor (el ejecutable de la carpeta de arriba) lo carga al abrirse.\n"
              "Puedes abrirlo en el editor de Source 2D, cambiarlo y guardarlo aqui sin volver a exportar.\n", f);
        fclose(f);
    }
    info = kb((long)(eng.size() + js.size())) + ", en una carpeta";
    return true;
}
}
